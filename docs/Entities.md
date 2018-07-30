#### Memory Block [TMemoryBlock]
Atomic chunk of memory accessed via device protocol, which means that any part of memory block
can't be obtained without obtaining whole block.

#### IR Value Layer [TIRValue and its subclasses]
A hierarchy of classes that holds (byte sequence) <-> (value) conversion logic.
Produces top level value by turning byte sequence into meaningful value and vice versa.
May represent numeric and textual value.
Formatting is done here. For text formatting is not performed.

#### IR Value Assembler [TIR*ValueAssembler]
A subset of IR Value Layer that encapsulate (byte sequence) <-> (typed value) conversion logic </br>
Assembled typed value (i.e. int, double, vector) is used to store value that later will be converted
to string by IR*Value class that inherited from the assembler class.

#### Virtual Register [TVirtualRegister]
A formatted top level device data representation layer. Holds IR Value.
May refer to one or many memory blocks. Holds all information needed to compose
final value that is ready to be published to MQTT channel and to decompose value that needs to be
written to device.

#### IR Device Query [TIRDeviceQuery, TIRDeviceValueQuery]
Intermediate Representation Device Query encapsulates single operation to be performed on device
by storing exhaustive data needed to perform any supported operation by any device and
by providing functions to accept response from device.</br>
It is a bridge between a concrete device implementation and the rest of the system

#### IR Device Memory View Layer [TIRDeviceMemoryView, TIRDeviceMemoryBlockView, TIRDeviceValueView]
A set of classes that provide managed access to raw memory written by a concrete device protocol
implementation. They use provided by protocol implementation memory layout to unify access to
separate memory blocks and values stored in those blocks.</br>
Associativity: TIRDeviceMemoryView holds one or more TIRDeviceMemoryBlockView which in turn holds
one or more TIRDeviceValueView.</br>
TIRDeviceMemoryView is used to access data section of response or request directly by splitting it on TIRDeviceMemoryBlockViews</br>
TIRDeviceMemoryBlockView is used to access single memory block</br>
TIRDeviceValueView is used to access single value of memory block (Protocol Value)
as regular C++ variable with platform endianness in mind.
It allows to trivially transform uncommonly packed values by protocols like mercury230
