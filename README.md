# Memory Reducer

A C++ utility for encoding and decoding tick-data and depth-data files into a
compact binary representation.

## Build

Use a C++17-compatible compiler, for example MinGW g++:

```powershell
g++ -O3 -std=c++17 encode.cpp -o encode_tickdata.exe
g++ -O3 -std=c++17 decode.cpp -o decode_tickdata.exe
```

## Usage

Encode tick data:

```powershell
.\encode_tickdata.exe data input.csv output.bin
```

Encode depth data:

```powershell
.\encode_tickdata.exe depth input.csv output.bin
```

Decode tick data:

```powershell
.\decode_tickdata.exe data input.bin output.csv
```

Decode depth data:

```powershell
.\decode_tickdata.exe depth input.bin output.csv
```

## Repository contents

The repository intentionally contains source code and documentation only.
Generated binaries and large market-data files are ignored.
