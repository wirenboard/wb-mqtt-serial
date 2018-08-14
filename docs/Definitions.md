# Terminology specific to this driver
#### Size
Memory size in bytes

#### Width
Memory size in bits

#### Protocol Value
A protocol value is a meaningful numeric value from protocol perspective.</br>
For modbus it is u16 register value.
For mercury200 - each u32 value in array is a protocol value

#### Formatting
Application of Scale, Offset and RoundTo to numeric value

#### Top Level Value
String that is either a formatted numeric value or text
that is going to be published to MQTT without any further changes
or was received from MQTT
