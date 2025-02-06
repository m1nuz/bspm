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

Run executable
```console
bspm run <dir>
```

Clean project and remove generated files
```console
bspm clean <dir>
```

## How to build

with g++
```console
g++ -std=c++23 -Wpedantic -Wall -Wextra -Werror bspm.cpp -O2 -lstdc++exp -o bspm
```

with clang++
```console
clang++ -std=c++23 -Wpedantic -Wall -Wextra -Werror bspm.cpp -O2 -o bspm
```
