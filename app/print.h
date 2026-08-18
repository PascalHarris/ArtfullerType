#ifndef ARTFULTYPE_PRINT_H
#define ARTFULTYPE_PRINT_H

#include "document.h"

/* print.c */
Boolean DoPageSetup(void);
void DoPrint(void);
void EnsurePageBreaks(DocumentPtr doc);
void InvalidatePagination(DocumentPtr doc);

#endif
