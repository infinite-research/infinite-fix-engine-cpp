.section .data.rel.ro.local,"aw",@progbits
.p2align 3
.globl irfq_infinite_embedded_dictionaries_v1
.hidden irfq_infinite_embedded_dictionaries_v1
.type irfq_infinite_embedded_dictionaries_v1, @object
irfq_infinite_embedded_dictionaries_v1:
  .quad _binary_FIX40_xml_start, _binary_FIX40_xml_size
  .quad _binary_FIX41_xml_start, _binary_FIX41_xml_size
  .quad _binary_FIX42_xml_start, _binary_FIX42_xml_size
  .quad _binary_FIX43_xml_start, _binary_FIX43_xml_size
  .quad _binary_FIX44_xml_start, _binary_FIX44_xml_size
  .quad _binary_FIX50_xml_start, _binary_FIX50_xml_size
  .quad _binary_FIX50SP1_xml_start, _binary_FIX50SP1_xml_size
  .quad _binary_FIX50SP2_xml_start, _binary_FIX50SP2_xml_size
  .quad _binary_FIXT11_xml_start, _binary_FIXT11_xml_size
.size irfq_infinite_embedded_dictionaries_v1, .-irfq_infinite_embedded_dictionaries_v1

.section .note.GNU-stack,"",@progbits
