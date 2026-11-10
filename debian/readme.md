## building deb package
```bash
mkdir build
cd build
cmake ..
make
../debian/make-deb-pkg.sh 1.0.1
```

## release package
Publishing a GitHub release (`gh release create`) runs the
[`release-debs`](../.github/workflows/release-debs.yml) workflow, which builds
the amd64 package in a `debian:bookworm` container and attaches it to the
release. To rebuild it for an existing release, run the workflow manually with
the release tag. The build uses the scripts at that tag, so this only works for
releases made after the workflow was added.
