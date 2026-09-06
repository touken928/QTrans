# Local Conan packages

Place each repository-owned Conan package in its own direct child directory:

```text
libs/
  package-name/
    conanfile.py
    CMakeLists.txt
    ...
```

Declare every package path explicitly in the root `conanws.yml`. Workspace
members are exposed as editable packages while Conan commands run inside this
repository. Each recipe must declare (or resolve) both `name` and `version`.

Add a package reference to `app/conanfile.py` only when QTrans consumes it.
