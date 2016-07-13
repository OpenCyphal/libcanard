# Universal CAN Library (avr-can-lib)

Universal CAN Library for AVR controllers (AT90CAN, MCP2515, SJA1000) by RCA ([Roboterclub Aachen e.V.](http://www.roboterclub.rwth-aachen.de/))

Github: https://github.com/dergraaf/avr-can-lib

Built for **at90can128** with avr-gcc (GCC) 4.9.2 and following configuration:

```c
#define	SUPPORT_EXTENDED_CANID  1
#define	SUPPORT_TIMESTAMPS      1

#define	SUPPORT_MCP2515         0
#define	SUPPORT_AT90CAN         1
#define	SUPPORT_SJA1000         0

#define CAN_RX_BUFFER_SIZE      16
#define CAN_TX_BUFFER_SIZE      8

#define CAN_FORCE_TX_ORDER      1
```
