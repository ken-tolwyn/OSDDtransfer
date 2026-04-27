#!/bin/bash

vHauler=1.2.5
platform=linux
arch=amd64

curl -sOL https://github.com/hauler-dev/hauler/releases/download/v${vHauler}/hauler_${vHauler}_${platform}_${arch}.tar.gz
tar -xf hauler_${vHauler}_${platform}_${arch}.tar.gz
cp hauler_${vHauler}_${platform}_${arch}.tar.gz /trunk/transfer/
chmod +x hauler

cat <<EOF > hauler-manifest.yaml
---
apiVersion: content.hauler.cattle.io/v1
kind: Charts
metadata:
  name: hauler-charts
spec:
  charts:
  - name: hauler-helm
    repoURL: oci://ghcr.io/hauler-dev
  - name: zot
    repoURL: http://zotregistry.dev/helm-charts
  - name: gitlab-runner
    repoURL: https://charts.gitlab.io
  - name: spegel
    repoURL: oci://ghcr.io/spegel-org/helm-charts
    version: 0.3.0
  - name: cert-manager
    repoURL: https://charts.jetstack.io
  - name: k8s-gateway
    repoURL: https://ori-edge.github.io/k8s_gateway
---
apiVersion: content.hauler.cattle.io/v1
kind: Images
metadata:
  name: hauler-cluster-images
spec:
  images:
  - name: ghcr.io/project-zot/zot-linux-amd64:v2.1.6
  - name: ghcr.io/spegel-org/spegel:v0.3.0
$(kubeadm config images list --kubernetes-version stable-1.29 | sed 's/^/  - name: /')
EOF

./hauler store sync -s /trunk/hauler -f hauler-manifest.yaml -p linux/amd64
./hauler store save -s /trunk/hauler -f /trunk/transfer/hauler.tar.zst -p linux/amd64
