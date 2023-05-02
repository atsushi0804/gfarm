#!/bin/sh
set -xeu

# install and create simple ca
GSDIR=/etc/grid-security
sudo grid-ca-create -noint -subject "cn=CA, ou=GfarmTest, o=Grid" -nobuild
rm -f openssl_req.log
for f in $GSDIR/certificates/*.0
do
	HASH=$(openssl x509 -hash -in $f -noout)
	[ -f $GSDIR/certificates/grid-security.conf.$HASH ] && break
done
sudo grid-default-ca -ca $HASH

mkdir -p ~/local/certs
cp -p $GSDIR/certificates/$HASH.* \
	$GSDIR/certificates/*.$HASH ~/local/certs
for h in c2 c3 c4
do
	ssh $h sudo mkdir -p $GSDIR/certificates
	ssh $h sudo cp local/certs/* $GSDIR/certificates
	ssh $h sudo grid-default-ca -ca $HASH
done
rm -rf ~/local/certs

# host cert
PASS=globus
DIGEST=sha256
[ -f $GSDIR/hostcert.pem ] || {
	yes | sudo grid-cert-request -host c1
	echo $PASS | sudo grid-ca-sign -in $GSDIR/hostcert_request.pem \
		-out $GSDIR/hostcert.pem -passin stdin -md $DIGEST
}

SERVICE=gfsd
[ -f $GSDIR/$SERVICE/${SERVICE}cert.pem ] || {
	yes | sudo grid-cert-request -service $SERVICE -host c1
	echo $PASS | sudo grid-ca-sign \
		-in $GSDIR/$SERVICE/${SERVICE}cert_request.pem \
		-out $GSDIR/$SERVICE/${SERVICE}cert.pem \
		-passin stdin -md $DIGEST
	sudo chown -R _gfarmfs:_gfarmfs $GSDIR/$SERVICE
}

for h in c2 c3 c4
do
	ssh $h test -f $GSDIR/hostcert.pem && continue

	mkdir -p ~/local/$h
	ssh $h "yes | sudo grid-cert-request -host $h"
	ssh $h cp $GSDIR/hostcert_request.pem local/$h
	echo $PASS | sudo grid-ca-sign -in ~/local/$h/hostcert_request.pem \
		-out ~/local/$h/hostcert.pem -passin stdin -md $DIGEST
	ssh $h sudo cp local/$h/hostcert.pem $GSDIR
	rm -rf ~/local/$h
done

for h in c2 c3 c4
do
	ssh $h test -f $GSDIR/$SERVICE/${SERVICE}cert.pem && continue

	mkdir -p ~/local/$h
	ssh $h "yes | sudo grid-cert-request -service $SERVICE -host $h"
	ssh $h cp $GSDIR/$SERVICE/${SERVICE}cert_request.pem local/$h
	echo $PASS | sudo grid-ca-sign \
		-in ~/local/$h/${SERVICE}cert_request.pem \
		-out ~/local/$h/${SERVICE}cert.pem -passin stdin -md $DIGEST
	ssh $h sudo cp local/$h/${SERVICE}cert.pem  $GSDIR/$SERVICE
	ssh $h sudo chown -R _gfarmfs:_gfarmfs $GSDIR/$SERVICE
	rm -rf ~/local/$h
done
