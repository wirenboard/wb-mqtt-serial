# Some internal logic in human language
#### How Device Queries are generated from register configs
It is done in 3 main steps:
1) Associate top level value described by config with memory blocks, creating
   associated memory block sets
2) Merge given associated memory block sets, taking into account max hole and max regs
3) Create queries from merged memory block sets

#### Top level value distribution over memory blocks
Logic is implemented at memory_block_factory.cpp
At this stage we determine how many memory blocks is needed to represent value, what are their addresses and what bits of memory block are corresponding to this top level value. All of this is determined taking into account bit offset, bit width and word order.

    (top level value, address: 0, offset: 8, width: 32, word order: "big-endian")
                     |bbbbbbbb  bbbbbbbb bbbbbbbb  bbbbbbbb|
                      ||||||||  |||||||| ||||||||  ||||||||
            |bbbbbbbb bbbbbbbb||bbbbbbbb bbbbbbbb||bbbbbbbb bbbbbbbb|
                (address 0)       (address 1)        (address 2)
                            (3 16-bit memory blocks)


    (top level value, address: 0, offset: 8, width: 32, word order: "little-endian")
                     |bbbbbbbb  bbbbbbbb bbbbbbbb  bbbbbbbb|
                      ||||||||  |||||||| ||||||||  ||||||||
            |bbbbbbbb bbbbbbbb||bbbbbbbb bbbbbbbb||bbbbbbbb bbbbbbbb|
                (address 2)       (address 1)        (address 0)
                            (3 16-bit memory blocks)

#### Maintained invariants
1) **Top level value's atomicity:** All memory blocks of given top level value are read/written in one query
2) **Memory block's cache policy:** A cache is allocated for writable memory block if it has writable virtual register associated with it that's not covering memory block entirely
