leveldb Log format
==================

日志文件内容是一个32KB块的序列。

每个块由一系列记录组成。

    block := record* trailer?
    record :=
      checksum: uint32     // crc32c of type and data[] ; little-endian 小端
      length: uint16       // little-endian 小端
      type: uint8          // One of FULL, FIRST, MIDDLE, LAST
      data: uint8[length]

记录由校验，长度，类型，数据构成。

record永远不会在block的最后6个字节内开始, 应该填充0。(Record长度最短为7B)

旁白：如果当前块中只剩下7个字节，并且添加了一个新的非零长度记录，则编写器必须发出FIRST record（其中包含零字节的用户数据）以填充块的尾随7个字节，然后在后续块中发出所有用户数据。

    FULL == 1
    FIRST == 2
    MIDDLE == 3
    LAST == 4

记录类型有四种

FULL record表明包含了完整的用户记录。

用户记录太大可能超过块大小，就需要分别存储，FIRST记录第一段用户记录，LAST记录最后一段，MIDDLE记录中间所有段。

    A: length 1000
    B: length 97270
    C: length 8000

A作为FULL记录存储在block 1中。

B将被分成三个片段：第一个片段占据block 1的其余部分，第二个片段占据block 2的全部，第三个片段占据block 3的前缀。这将使block 3中留有6B的空闲空间，该块将作为尾部留空。

C作为FULL类型的record存储在block 4中

## recordio格式的好处：

1. 我们不需要任何启发式方法即可重新同步，只需转到下一个块边界并进行扫描即可。如果存在损坏，跳至下一个块。作为额外好处，当一个日志文件的部分内容作为记录嵌入到另一个日志文件中时，我们不会困惑。

2. 在近似边界处进行分割很简单（例如，针对mapreduce）：查找下一个块边界并跳过记录，直到我们找到FULL或FIRST记录为止。

3. 对于大型记录，我们不需要额外的缓冲。

## recordio格式的缺点：

1. 没有打包小记录。 可以通过添加新的记录类型来解决此问题，因此这是当前实现的缺点，不一定是格式。

2. 无压缩。 同样，可以通过添加新的记录类型来解决此问题。

