# pascal-compiler

This is a Pascal compiler that parses a small subset of the Pascal programming language. It supports procedures, variable declarations, and other basic constructs. It does not support functions in their entirety (e.g., it does not support returning values). It is written entirely in C++ and complies with ANSI C++20. The compiler was developed as an academic project when I was earning my bachelor's degree in university.

The program depends on a few single-header libraries which are included. Mainly, it depends on [Nlohmann/JSON](https://github.com/nlohmann/json), the [Inja template library](https://github.com/pantor/inja), and the [Popl argument parsing library](https://github.com/badaix/popl).

The program takes as input any number of files which must be valid Pascal source code. If no files are provided, the program assumes that your code is in "code.txt". For each file, the code is evaluated and a C file is generated containing inline 32-bit x86 assembly that you can run through MSVC to produce a final executable program. For each file, the parser indicates whether the code was 100-percent valid or was malformed in some manner, and also indicates the total number of tokens and the number of tokens that were parsed before a termination condition occurred.

To build this program, you need only a C++ compiler that supports C++20. Test files are available if you wish to determine that the compiler functions as intended.

