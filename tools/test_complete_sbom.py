"""Run with python3 -m unittest discover -s tools -p 'test_complete_sbom.py'."""

import copy
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import complete_sbom as sbom


class InventoryTests(unittest.TestCase):
    def test_actual_vendor_inventory_and_test_only_scope(self):
        root = Path(__file__).resolve().parents[1]
        self.assertEqual(
            {p["name"]: p["versionInfo"] for p in sbom.vendor_packages(root)},
            {
                "pugixml": "1.14",
                "double-conversion": "2.0.1",
                "scope_guard": "1.1.0",
                "catch2": "3.4.0",
            },
        )
        self.assertEqual(
            {p["name"] for p in sbom.vendor_packages(root, installed=True)},
            {"pugixml", "double-conversion", "scope_guard"},
        )

    def test_missing_or_changed_vendor_is_not_a_clean_inventory(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with self.assertRaisesRegex(ValueError, "bytes changed or missing"):
                sbom.vendor_packages(root)
            (root / "link").symlink_to("missing-target")
            with patch.object(
                sbom,
                "VENDORS",
                (("link", "1", ("link",), "0" * 64, "upstream", "MIT"),),
            ):
                with self.assertRaisesRegex(ValueError, "regular in-tree"):
                    sbom.vendor_packages(root)
        with patch.object(
            sbom,
            "VENDORS",
            (("changed", "1", ("AGENTS.md",), "0" * 64, "upstream", "MIT"),),
        ):
            with self.assertRaisesRegex(ValueError, "bytes changed or missing"):
                sbom.vendor_packages(Path(__file__).resolve().parents[1])

    def test_unresolved_empty_or_non_tls_linkage_fails(self):
        for value in (
            "",
            "libssl.so.3 => not found",
            "statically linked",
            "/lib/libc.so.6 (0xabc)",
        ):
            with self.subTest(value=value), self.assertRaises(ValueError):
                sbom.linked_paths(value)

    def test_real_linkage_syntax(self):
        value = "\tlinux-vdso.so.1 (0xabcd)\n\tlibssl.so.3 => /lib/libssl.so.3 (0xab)\n\t/lib/ld-linux.so.2 (0xbc)"
        self.assertEqual(
            sbom.linked_paths(value),
            [Path("/lib/libssl.so.3"), Path("/lib/ld-linux.so.2")],
        )

    def test_runtime_package_metadata_and_deduplication(self):
        output = "libssl.so.3 => /lib/libssl.so.3 (0xab)\nlibcrypto.so.3 => /lib/libcrypto.so.3 (0xbc)"

        def command(*args):
            if args[0] == "ldd":
                return output
            if args[1] == "--search":
                return f"libssl3t64:arm64: {args[2]}"
            return "libssl3t64\t3.5.6-1~deb13u2\tarm64\topenssl\t3.5.6-1~deb13u2\tinstalled"

        with patch.object(sbom, "run", side_effect=command):
            packages = sbom.runtime_packages(Path("library"), "debian")
        self.assertEqual(len(packages), 1)
        self.assertEqual(
            packages[0]["externalRefs"][0]["referenceLocator"],
            "pkg:deb/debian/libssl3t64@3.5.6-1~deb13u2?arch=arm64&upstream=openssl%403.5.6-1~deb13u2",
        )
        with patch.object(
            sbom, "run", side_effect=[output, "owner: /lib/libssl.so.3", "incomplete"]
        ):
            with self.assertRaisesRegex(ValueError, "incomplete"):
                sbom.runtime_packages(Path("library"), "debian")

    def test_diversion_is_not_ownership_and_ambiguity_fails(self):
        output = "libssl.so.3 => /lib/libssl.so.3 (0xab)"
        with patch.object(
            sbom,
            "run",
            side_effect=lambda *args: (
                output if args[0] == "ldd" else f"diversion by owner from: {args[-1]}"
            ),
        ):
            with self.assertRaisesRegex(ValueError, "no package owner"):
                sbom.runtime_packages(Path("library"), "debian")
        with patch.object(
            sbom,
            "run",
            side_effect=[output, "one: /lib/libssl.so.3\ntwo: /lib/libssl.so.3"],
        ):
            with self.assertRaisesRegex(ValueError, "ambiguous"):
                sbom.runtime_packages(Path("library"), "debian")

    def test_preserve_syft_and_reject_empty_or_duplicate_completion(self):
        document = {
            "spdxVersion": "SPDX-2.3",
            "packages": [{"SPDXID": "root", "name": "original"}],
            "relationships": [
                {
                    "spdxElementId": "SPDXRef-DOCUMENT",
                    "relationshipType": "DESCRIBES",
                    "relatedSpdxElement": "root",
                }
            ],
            "creationInfo": {"creators": ["Tool: syft"]},
            "documentNamespace": "https://example.test/sbom",
        }
        entry = sbom.package("dependency", "1", "pkg:generic/dependency@1", "test")
        result = sbom.complete(copy.deepcopy(document), [entry])
        self.assertEqual(result["packages"][0], document["packages"][0])
        self.assertEqual(
            result["relationships"][-1]["relatedSpdxElement"], entry["SPDXID"]
        )
        self.assertNotEqual(result["documentNamespace"], document["documentNamespace"])
        for additions in ([], [entry]):
            with self.assertRaises(ValueError):
                sbom.complete(copy.deepcopy(result), additions)
        with self.assertRaises(ValueError):
            sbom.package("unknown", "NOASSERTION", "pkg:generic/unknown", "test")


if __name__ == "__main__":
    unittest.main()
