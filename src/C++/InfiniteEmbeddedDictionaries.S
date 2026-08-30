.section .data.rel.ro.local,"aw",@progbits
.p2align 3
.globl irfq_infinite_embedded_dictionaries_v1
.hidden irfq_infinite_embedded_dictionaries_v1
.type irfq_infinite_embedded_dictionaries_v1, @object
irfq_infinite_embedded_dictionaries_v1:
  .quad _binary_irfq_infinite_fixt11_xml_start, _binary_irfq_infinite_fixt11_xml_size
  .quad _binary_irfq_infinite_rfq_1_0_0_ep299_xml_start, _binary_irfq_infinite_rfq_1_0_0_ep299_xml_size
.size irfq_infinite_embedded_dictionaries_v1, .-irfq_infinite_embedded_dictionaries_v1

.section .note.GNU-stack,"",@progbits
