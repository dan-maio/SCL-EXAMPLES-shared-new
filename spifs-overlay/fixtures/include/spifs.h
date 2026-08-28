#ifndef SPIFS_H
#define SPIFS_H
#ifdef __cplusplus
extern "C" {
#endif
/* SPI-flash flatfs, as a loadable library (W25Q64 driver + flatfs on top). */
int      spifs_init(void);                                          /* 0 = W25Q64 found */
int      spifs_format(void);                                        /* 0 = ok */
int      spifs_create(const char* name, const void* data, unsigned len); /* 0 = ok */
int      spifs_delete(const char* name);                            /* 0 = ok, -1 = absent */
unsigned spifs_count(void);                                         /* live file count */
int      spifs_list(unsigned idx, char* name_out, unsigned* size_out); /* 0 = ok, -1 = end */
int      spifs_read(const char* name, void* buf, unsigned cap, unsigned* out_len); /* 0 = ok */
#ifdef __cplusplus
}
#endif
#endif
