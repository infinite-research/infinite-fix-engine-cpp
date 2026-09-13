#!/bin/sh
# Disposable test credentials only. Never use these roots outside local tests.
set -eu
umask 077
destination=${1:?Usage: generate-test-pki.sh BUILD_DIRECTORY/test-runtime/certs}
case "$destination" in */test-runtime/certs) ;; *) exit 2 ;; esac
mkdir -p "$destination"
cd "$destination"
test ! -e ca.key || { echo 'Refusing to overwrite existing test PKI' >&2; exit 2; }
mkdir issued empty
touch index
printf '1000\n' > serial
printf '1000\n' > crlnumber
cat > ca.cnf <<'EOF'
[ca]
default_ca = test
[test]
database = index
new_certs_dir = issued
certificate = ca.crt
private_key = ca.key
serial = serial
crlnumber = crlnumber
default_md = sha256
default_days = 2
default_crl_days = 2
policy = policy
unique_subject = no
[policy]
commonName = supplied
EOF
for root in ca unrelated; do
  openssl req -new -newkey rsa:2048 -nodes -x509 -days 2 -subj "/CN=QuickFIX disposable $root" \
    -addext 'basicConstraints=critical,CA:TRUE' -addext 'keyUsage=critical,keyCertSign,cRLSign' \
    -keyout "$root.key" -out "$root.crt" 2>/dev/null
done
leaf() {
  name=$1 san=$2 purpose=$3
  openssl req -new -newkey rsa:2048 -nodes -subj /CN=localhost \
    -keyout "$name.key" -out "$name.csr" 2>/dev/null
  printf 'basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature,keyEncipherment\nextendedKeyUsage=%s\nsubjectAltName=%s\n' \
    "$purpose" "$san" > leaf.cnf
  shift 3
  openssl ca -batch -notext -config ca.cnf -extfile leaf.cnf -in "$name.csr" -out "$name.crt" "$@" 2>/dev/null
  cp leaf.cnf "$name.cnf"
}
leaf server 'DNS:localhost,IP:127.0.0.1,IP:::1' serverAuth
leaf wrong-dns 'DNS:wrong.example,IP:127.0.0.1' serverAuth
leaf wrong-ip 'DNS:localhost,IP:192.0.2.1' serverAuth
leaf client 'DNS:client.example,IP:127.0.0.1' clientAuth
leaf wrong-client-eku 'DNS:client.example' serverAuth
leaf wildcard-client 'DNS:*.example' clientAuth
leaf expired 'DNS:localhost,IP:127.0.0.1' serverAuth -startdate 20000101000000Z -enddate 20000102000000Z
leaf revoked 'DNS:localhost,IP:127.0.0.1' serverAuth
openssl ca -batch -config ca.cnf -revoke revoked.crt 2>/dev/null
openssl ca -batch -config ca.cnf -gencrl -out ca.crl 2>/dev/null
openssl x509 -req -in server.csr -CA unrelated.crt -CAkey unrelated.key -set_serial 42 \
  -days 2 -extfile server.cnf -out unrelated-server.crt 2>/dev/null
openssl x509 -req -in client.csr -CA unrelated.crt -CAkey unrelated.key -set_serial 43 \
  -days 2 -extfile client.cnf -out unrelated-client.crt 2>/dev/null
openssl ca -batch -config ca.cnf -cert unrelated.crt -keyfile unrelated.key -gencrl -out unrelated.crl 2>/dev/null
mkdir crl-directory
cp ca.crl unrelated.crl unrelated.crt crl-directory/
openssl rehash crl-directory
echo 'Generated disposable two-day test PKI'
