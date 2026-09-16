#!/usr/bin/env python3
"""Complete Syft SPDX with hash-verified vendors and Ubuntu/Debian ELF dependencies."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
from urllib.parse import quote


# Digests cover sorted repository-relative paths, NUL, bytes, NUL.
# The upstream version is an advisory-matching baseline, not an assertion
# that a locally modified vendor is identical to that upstream release.
VENDORS = (
    (
        "pugixml",
        "1.14",
        ("src/C++/pugixml.cpp", "src/C++/pugixml.hpp", "src/C++/pugiconfig.hpp"),
        "ceed043a63f4104f805acd650cea89d3f792796c7190ad5b2ef40eae50996082",
        "https://github.com/zeux/pugixml/tree/v1.14",
        "MIT",
    ),
    (
        "double-conversion",
        "2.0.1",
        ("src/C++/double-conversion/*.h", "src/C++/double-conversion/*.cc"),
        "0d0ee3379eabb9fe7d7f21536513ea0c07fb675d8e8df47c0ddc47c4072604cf",
        "https://github.com/google/double-conversion/tree/2fb03de56faa32bbba5e02222528e7b760f71d77",
        "BSD-3-Clause",
    ),
    (
        "scope_guard",
        "1.1.0",
        ("src/C++/scope_guard.hpp",),
        "9101b84d570dfd4a6b95233f7c26ebf8fd64731c7f700980dc02e3ef12c8a985",
        "https://github.com/ricab/scope_guard/tree/ee296e156cf01f9b8cf223f3f0b39501a6d4cb82",
        "Unlicense",
    ),
    (
        "catch2",
        "3.4.0",
        ("src/C++/test/catch_amalgamated.cpp", "src/C++/test/catch_amalgamated.hpp"),
        "c4034484057b2434298d46b88b7b61043ca44d39fd26dff733cf7c726549ce09",
        "https://github.com/catchorg/Catch2/tree/v3.4.0",
        "BSL-1.0",
    ),
)


def package(name, version, purl, source, license_id="NOASSERTION"):
    if not name or not version or version == "NOASSERTION":
        raise ValueError("dependency identity/version is missing")
    return {
        "SPDXID": "SPDXRef-dependency-"
        + hashlib.sha256(purl.encode()).hexdigest()[:20],
        "name": name,
        "versionInfo": version,
        "filesAnalyzed": False,
        "downloadLocation": "NOASSERTION",
        "licenseConcluded": "NOASSERTION",
        "licenseDeclared": license_id,
        "copyrightText": "NOASSERTION",
        "sourceInfo": source,
        "externalRefs": [
            {
                "referenceCategory": "PACKAGE-MANAGER",
                "referenceType": "purl",
                "referenceLocator": purl,
            }
        ],
    }


def vendor_packages(root, installed=False):
    packages = []
    for name, version, patterns, expected, upstream, license_id in VENDORS:
        if installed and name == "catch2":  # Test-only, not linked into libquickfix.
            continue
        digest = hashlib.sha256()
        for path in sorted({p for pattern in patterns for p in root.glob(pattern)}):
            if path.is_symlink() or not path.resolve().is_relative_to(root.resolve()):
                raise ValueError(f"{name}: vendor input must be a regular in-tree file")
            digest.update(
                path.relative_to(root).as_posix().encode()
                + b"\0"
                + path.read_bytes()
                + b"\0"
            )
        if digest.hexdigest() != expected:
            raise ValueError(
                f"{name}: vendor bytes changed or missing; reverify inventory provenance"
            )
        packages.append(
            package(
                name,
                version,
                f"pkg:generic/{name}@{version}",
                f"Hash-verified vendored snapshot sha256:{expected}; upstream advisory baseline {upstream}. "
                "Local modifications are not covered by upstream advisory matching.",
                license_id,
            )
        )
    return packages


def run(*args):
    return subprocess.check_output(args, text=True, stderr=subprocess.PIPE).strip()


def linked_paths(output):
    paths = []
    for line in output.splitlines():
        if line.strip().startswith("linux-vdso."):
            continue  # Kernel mapping, not a disk dependency.
        match = re.fullmatch(r"\s*(?:\S+ => )?(/\S+) \(0x[0-9a-fA-F]+\)\s*", line)
        if not match:
            raise ValueError(f"unresolved/unrecognised ldd entry: {line}")
        paths.append(Path(match[1]))
    if not paths:
        raise ValueError("empty linked dependency inventory")
    if not any(p.name.startswith("libssl.so.") for p in paths):
        raise ValueError("installed assurance build must link OpenSSL")
    return paths


def runtime_packages(library, distro):
    packages = {}
    for path in linked_paths(run("ldd", str(library))):
        for candidate in dict.fromkeys((path, path.resolve())):
            try:
                ownership = run("dpkg-query", "--search", str(candidate))
            except subprocess.CalledProcessError:
                continue
            # dpkg may print diversion records, which are not file ownership.
            owners = {
                line.rsplit(": ", 1)[0]
                for line in ownership.splitlines()
                if line.endswith(": " + str(candidate))
                and re.fullmatch(
                    r"[a-z0-9][a-z0-9+.-]*(?::[a-z0-9-]+)?", line.rsplit(": ", 1)[0]
                )
            }
            if len(owners) > 1:
                raise ValueError(f"ambiguous package owner for {path}")
            if owners:
                break
        else:
            raise ValueError(f"no package owner for linked library {path}")
        owner = owners.pop()
        fields = run(
            "dpkg-query",
            "--show",
            "--showformat=${Package}\t${Version}\t${Architecture}\t"
            "${source:Package}\t${source:Version}\t${db:Status-Status}",
            owner,
        ).split("\t")
        if (
            len(fields) != 6
            or any(not value for value in fields)
            or fields[-1] != "installed"
        ):
            raise ValueError(f"incomplete installed package metadata for {owner}")
        name, version, arch, source_name, source_version, _ = fields
        purl = (
            f"pkg:deb/{distro}/{name}@{quote(version, safe='')}?arch={quote(arch, safe='')}"
            f"&upstream={quote(source_name + '@' + source_version, safe='')}"
        )
        packages[purl] = package(
            name,
            version,
            purl,
            f"dpkg owner of {path}; source {source_name} {source_version}",
        )
    return list(packages.values())


def complete(document, additions):
    if document.get("spdxVersion") != "SPDX-2.3":
        raise ValueError("expected Syft SPDX-2.3 document")
    roots = [
        r["relatedSpdxElement"]
        for r in document.get("relationships", [])
        if r.get("spdxElementId") == "SPDXRef-DOCUMENT"
        and r.get("relationshipType") == "DESCRIBES"
    ]
    ids = {p["SPDXID"] for p in document["packages"]}
    if len(roots) != 1 or roots[0] not in ids or not additions:
        raise ValueError("missing SBOM root or dependency inventory")
    for entry in additions:
        if entry["SPDXID"] in ids:
            raise ValueError("dependency already present; use the original Syft input")
        ids.add(entry["SPDXID"])
        document["packages"].append(entry)
        document["relationships"].append(
            {
                "spdxElementId": roots[0],
                "relationshipType": "DEPENDS_ON",
                "relatedSpdxElement": entry["SPDXID"],
            }
        )
    document["creationInfo"]["creators"].append("Tool: quickfix-complete-sbom")
    document["documentNamespace"] += (
        "-completed-"
        + hashlib.sha256(json.dumps(additions, sort_keys=True).encode()).hexdigest()
    )
    return document


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument(
        "--source", type=Path, default=Path(__file__).resolve().parents[1]
    )
    parser.add_argument("--library", type=Path)
    parser.add_argument("--distro", choices=("ubuntu", "debian"))
    args = parser.parse_args()
    if bool(args.library) != bool(args.distro):
        parser.error("--library and --distro must be supplied together")
    if args.input.resolve() == args.output.resolve():
        parser.error("retain the original Syft input; output must differ")
    additions = vendor_packages(args.source, installed=bool(args.library))
    if args.library:
        additions += runtime_packages(args.library.resolve(strict=True), args.distro)
    document = complete(json.loads(args.input.read_text()), additions)
    args.output.write_text(json.dumps(document, indent=2) + "\n")
    print(f"Completed {args.output}: {len(additions)} evidenced dependencies added")


if __name__ == "__main__":
    main()
