from .Primitives import TextEncoding, get_decoding, from_int, as_bytes

class Msbt():
    MAGIC : bytes = b'\x4D\x73\x67\x53\x74\x64\x42\x6E'

    ATR1_MAGIC : bytes = b'\x31\x52\x54\x41'
    LBL1_MAGIC : bytes = b'\x4C\x42\x4C\x31'
    TXT2_MAGIC : bytes = b'\x54\x58\x54\x32'

    FILE_HEADER_SIZE : int = 32

    BLOCK_HEADER_SIZE : int = 16

    endian : str
    encoding : TextEncoding
    version : bytes
    blocks : bytes
    buckets: bytes
    label_section : bytes

    msbt : dict = {}

    def __init__(self, file: bytes):
        # File Header
        position = 0
        self.endian = "little" if file[8:10] == b'\xff\xfe' else "big"
        self.encoding = from_int(file[0xC:0xD])
        #print(hex(len(file)))
        #print(file[0:8])
        #print(self.MAGIC.to_bytes(8, self.endian) == file[0:8])
        #print(file[8:10])
        #print(file[0xC:0xD])
        self.version = file[0xD:0xE]
        #print(file[0xD:0xE])
        self.blocks = file[0xE:0x10]
        #print(file[0xE:0x10])
        #print(file[0x12:0x16])
        # Label Header
        position += self.FILE_HEADER_SIZE
        label_header = file[position:position + self.BLOCK_HEADER_SIZE]
        #print(label_header[0x0:0x4])
        label_block_size = int.from_bytes(label_header[0x4:0x8], self.endian)
        #print(label_block_size)
        position += self.BLOCK_HEADER_SIZE
        labels_block = file[position:position + label_block_size]
        buckets = int.from_bytes(labels_block[0x0:0x4], self.endian)
        self.buckets = labels_block[0x0:0x4]
        #print(buckets)
        #print(8 * buckets + 4)
        self.label_section = labels_block[4:8 * buckets + 4]
        labels = labels_block[8 * buckets + 4:]
        #print(labels)

        position += label_block_size
        between = 0

        while file[position + between:position + between + 1] == b'\xab':
            between += 1

        #print(between)
        position += between

        # Text Header
        text_header = file[position:position + self.BLOCK_HEADER_SIZE]
        text_block_size = int.from_bytes(text_header[0x4:0x8], self.endian)
        #print(text_block_size)
        position += self.BLOCK_HEADER_SIZE
        text_block = file[position:position + text_block_size]
        num_messages = int.from_bytes(text_block[0:4], self.endian)
        #print(num_messages)

        position += text_block_size

        message_data = text_block[4 + num_messages * 4:4 + num_messages * 4 + text_block_size - 3]

        offset = 0
        messages = []
        message = ""
        opened_tags = 0
        control_tags = []
        tags = []

        # Add tag support for UTF-8 encoded messages

        while offset < len(message_data):
            if message_data[offset:offset + 2] == b'\x00\x0e' or message_data[offset:offset + 1] == b'\x0e':
                opened_tags += 1
                #print("Entering control tag.")
                group = int.from_bytes(labels[offset:offset + 2], self.endian)
                offset += 2
                index = int.from_bytes(labels[offset:offset + 2], self.endian)
                offset += 2
                size = int.from_bytes(labels[offset:offset + 2], self.endian)
                offset += 2
                params = int.from_bytes(labels[offset:offset + size], self.endian)
                offset += size
                tags.append({"group" : group, "index" : index, "param_size" : size, "parameters" : params,
                    "start" : len(message)})
            elif opened_tags > 0 and offset + 6 < len(message_data) and message_data[offset:offset + 6] == b'\x00\x00\x00\x00\x00\x0f':
                opened_tags -= 1
                for i in range(len(tags), 0, -1):
                    if not "end" in tags[i]:
                        tags[i]["end"] = len(message)
                        break
                offset += 6
            else:
                message += message_data[offset: offset + 2].decode(get_decoding(self.encoding))
                if message_data[offset: offset + 2] == b'\x00\x00':
                    control_tags.append(tags.copy())
                    if opened_tags > 0:
                        raise Exception("Error: Not all message tags closed.")
                    tags = []
                    messages.append(message)
                    message = ""
                offset += 2


        message_data.decode(get_decoding(self.encoding)).split("\0")
        # add message tags
        num_traversed_messages = 0
        offset = 0
        self.msbt["labels"] = {}
        while offset < len(labels) and num_traversed_messages < num_messages:
            num_traversed_messages += 1
            label_length =  int.from_bytes(labels[offset:offset + 1], self.endian)
            offset += 1
            label_string = (labels[offset:offset + label_length]).decode("utf-8")
            offset += label_length
            self.msbt["labels"][label_string] = {
                "length" : label_length,
                "index" : int.from_bytes(labels[offset:offset + 4], self.endian),
                "message": messages[int.from_bytes(labels[offset:offset + 4], self.endian)]
            }

            if control_tags[self.msbt["labels"][label_string]["index"]]:
                self.msbt["labels"][label_string]["tags"] = control_tags[self.msbt["labels"][label_string]["index"]]

            offset += 4

        between = 0

        while file[position + between:position + between + 1] == b'\xab':
            between += 1

        position += between
        #print(between)
        #print(file[position:])
        #print(self.msbt)
        # Attribute Header ATR1
        # Text Styles Header TSY1


    def get_bytes(self) -> bytes:
        """ Returns the raw bytes of the Msbt instance for saving to files.
            Args:
                self: The Msbt instance.
            Return:
                The bytes stream of this Msbt after applying all modifications.
        """
        file_header: bytearray = bytearray()
        file_header += self.MAGIC
        file_header += b'\xff\xfe' if self.endian == "little" else b'\xfe\xff'
        file_header += b'\x00\x00'
        file_header += as_bytes(self.encoding)
        file_header += self.version
        file_header += self.blocks
        file_header += b'\x00\x00'
        # file size
        # padding

        label_header : bytearray = bytearray()
        label_header += self.LBL1_MAGIC

        label_block : bytearray = bytearray()
        label_block += self.buckets
        label_strings = list(self.msbt["labels"].keys())

        # for label in label_strings:
        #     label_block += calc_hash(label, 101).to_bytes(8, self.endian)
        label_block += self.label_section

        for label in self.msbt["labels"].keys():
            label_block += self.msbt["labels"][label]["length"].to_bytes(1, self.endian)
            label_block += str(label).encode()
            label_block += self.msbt["labels"][label]["index"].to_bytes(4, self.endian)

        #print(len(label_block) % 16)
        while len(label_block) % 16 != 0:
            label_block += b'\xab'

        label_header += len(label_block).to_bytes(4, self.endian)
        label_header += b'\x00\x00\x00\x00\x00\x00\x00\x00'



        text_header : bytearray = bytearray()
        text_header += self.TXT2_MAGIC

        text_block : bytearray = bytearray()
        text_block += len(self.msbt["labels"].keys()).to_bytes(4, self.endian)
        msg_by_index = []
        for label in self.msbt["labels"].keys():
            msg_by_index.append((self.msbt["labels"][label]["index"], self.msbt["labels"][label]["message"]))
            if "tags" in self.msbt["labels"][label]:
                raise Exception("Writing message tags is not yet supported.")

        msg_by_index = sorted(msg_by_index,key= lambda msg : msg[0])
        offset = 4 * len(self.msbt["labels"].keys()) + 4
        for msg in msg_by_index:
            text_block += offset.to_bytes(4, self.endian)
            offset += len(msg[1]) * 2
            #print(msg[1])

        # Add control tag support
        for msg in msg_by_index:
            text_block += msg[1].encode(get_decoding(self.encoding))

        text_header += len(text_block).to_bytes(4, self.endian)
        text_header += b'\x00\x00\x00\x00\x00\x00\x00\x00'

        #print(len(text_block) % 16)
        while len(text_block) % 16 != 0:
            text_block += b'\xab'

        # for i in range(16):
        #     text_block += b'\xab'

        # Add Attributes and Text Styles


        # All data blocks must be divisible by 16 ie size % 16 == 0 filler bytes b`\xab` can be added to accomplish this

        file_header += b"\x00\x00\x00\x00"
        file_header += b'\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00'
        data: bytearray = bytearray()
        data += file_header
        data += label_header
        data += label_block
        data += text_header
        data += text_block
        #print(len(data)%16)
        #print(hex(len(data)))
        data[18:22] = len(data).to_bytes(4, self.endian)

        #print(data)
        return data

    def is_equal(self, other : bytes):
        return self.get_bytes() == other

def calc_hash(label, num_buckets):
    """ Calculates the hash bucket for a given label.
            Args:
                .
            Return:
                .
    """
    h = 0
    for char in label:
        h = h * 0x492 + ord(char)
    return (h & 0xFFFFFFFF) % num_buckets
