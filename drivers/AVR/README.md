# Universal CAN Library (avr-can-lib)

 Universal CAN Library for AVR controllers (AT90CAN, MCP2515, SJA1000) by RCA ([Roboterclub Aachen e.V.](http://www.roboterclub.rwth-aachen.de/))

 Github: https://github.com/dergraaf/avr-can-lib

 ## How-to

 Adjust file *can_config.h* to your needs.
 In file *CMakeLists.txt* set your controller and corresponding frequency:

 ```
set(MCU   at90can128)
set(F_CPU 16000000)
 ```

 After finishing configuration:

```schell
$ cmake .
$ make
```

Include  
*avr-can-lib/can.h*  
*avr-can-lib/libcan.a*  
*can_config.h*  
in your project.
