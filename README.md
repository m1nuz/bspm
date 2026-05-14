# bspm
Experimental tool for building C++ with Modules

## How to use

Initialize project
```console
bspm init <dir>
```

### Example

```console
bspm init hello
```

Directory structure:
<pre>
├── hello
│   └── main.cpp
</pre>

Build project
```console
bspm build <dir>
```

Choose compiler
```console
bspm build <dir> -c g++
bspm build <dir> -c clang++
bspm build <dir> -c msvc
```

Run executable
```console
bspm run <dir>
```

Clean project and remove generated files
```console
bspm clean <dir>
```

## How to build

With g++:
```console
g++ -std=c++23 -Wpedantic -Wall -Wextra -Werror bspm.cpp -O2 -lstdc++exp -o bspm
```

On Windows, build `bspm.exe` with g++:
```console
g++ -std=c++23 -Wpedantic -Wall -Wextra -Werror bspm.cpp -O2 -lstdc++exp -o bspm.exe
```

With clang++:
```console
clang++ -std=c++23 -Wpedantic -Wall -Wextra -Werror bspm.cpp -O2 -o bspm
```

With MSVC, use a Visual Studio Developer terminal or run `VsDevCmd.bat` first:
```console
cl /std:c++latest /EHsc /nologo bspm.cpp /Febspm.exe
```
