# for MSYS2 usage, recommended building using mingw
g++ -o windows-quick-switch-x86_64.exe -O3 -march=native -mtune=native -flto -fomit-frame-pointer -DNDEBUG -s main.cpp -static -mwindows -municode -lgdi32 -lShcore
g++ -o windows-quick-switch-x86_64-debug.exe main.cpp -static -mconsole -municode -lgdi32 -lShcore
