g++ -o windows-quick-switch.exe -O3 -march=native -mtune=native -flto -fomit-frame-pointer -DNDEBUG -s main.cpp -mwindows -municode -lgdi32 -lShcore
