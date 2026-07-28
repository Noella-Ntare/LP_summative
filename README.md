
project1/
├── program.c
├── program                 <- stripped executable 


Commands

bashcd project1

# 1. Compile
gcc -Wall -O0 -fno-inline -o program program.c -lm

# 2. Run it
./program

# 3. Strip debug/symbol info
strip program



project2/


project2/
├── line_counter.asm
├── sensor_readings.txt   <- sample input file 
└── line_counter          

Commands

bashcd project2_asm

# 1. Create a sample input file to test with
printf '23.5\n24.1\n\n25.0\n\n\n22.8\n' > sensor_readings.txt

# 2. Assemble and link
nasm -f elf64 line_counter.asm -o line_counter.o
ld line_counter.o -o line_counter

# 3. Run it
./line_counter

Expected output (for the sample file above, which has 7 lines, 3 empty):

Total records: 7
Valid records: 4




project3/


project3/
├── sensor_analysis.c
├── setup.py                          
├── test_sensor_analysis.py           
└── sensor_analysis*.so               




Commands

bashcd project3

# 1. Build the extension (requires python3-dev / Python.h)
python3 setup.py build_ext --inplace

# 2. Run your test script
python3 test_sensor_analysis.py


Expected output: printed results for each function call, plus
confirmation that invalid inputs raise TypeError/ValueError rather
than crashing.


project4/


project4/
├── order_system.c
└── order_system        

Commands

bashcd project4

# 1. Compile
gcc -Wall -O2 -pthread -o order_system order_system.c

# 2. Run (pass a small order count so the demo doesn't take forever)
./order_system 8



Expected output: interleaved lines like:

[HH:MM:SS] [KITCHEN]  Order #1 prepared. Queue size = 1
[HH:MM:SS] [DELIVERY] Order #1 picked up. Queue size = 0
[HH:MM:SS] [MONITOR]  Orders prepared: X | Orders delivered: Y | Current queue size: Z

ending with a final totals line once the target order count is reached.


project5/

Files that should be in this folder when you're done:

project5/
├── server.c
├── client.c
├── server              
└── client              
Commands

bashcd project5

# 1. Compile both
gcc -Wall -O2 -pthread -o server server.c
gcc -Wall -O2 -o client client.c

# 2. Start the server (leave this running in its own terminal)
./server 5050

In separate terminals, run clients to demo each scenario:

bash# successful auth + reservation
./client 127.0.0.1 5050 alice Oscilloscope

# invalid/unregistered user -> should fail auth
./client 127.0.0.1 5050 mallory Oscilloscope

# conflicting reservation -> should be rejected, item already taken
./client 127.0.0.1 5050 bob Oscilloscope

# run two clients at the same time from two terminals to show
# multiple simultaneous connections
./client 127.0.0.1 5050 dave "Logic Analyzer"
./client 127.0.0.1 5050 erin Spectrometer
