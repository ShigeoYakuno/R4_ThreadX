#ifndef VECTOR_TABLE_H_
#define VECTOR_TABLE_H_

/* Must be called first thing in hal_entry(), before any peripheral that relies on an
 * interrupt (e.g. uart_init()) is set up. See vector_table.c for why this is needed. */
void copy_vectors_to_ram(void);

#endif /* VECTOR_TABLE_H_ */
