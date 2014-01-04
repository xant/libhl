/**
 * @file rbb.h
 *
 * @brief Ring buffers
 *
 * Ringbuffer implementation store/access arbitrary binary data
 *
 * @todo allow to register i/o filters to be executed at read/write time
 *
 */

#ifndef __RBB_H__
#define __RBB_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef struct __rbb_s rbb_t;

/**
 * @brief Create a new ringbuffer
 * @param size : The size of the ringbuffer (in bytes)
 * @return     : A pointer to an initialized rbb_t structure
 */
rbb_t *rbb_create(int size);

/**
 * @brief Skip the specified amount of bytes
 * @param rbb  : A valid pointer to a rbb_t structure
 * @param size :Tthe number of bytes to skip
 */
void rbb_skip(rbb_t *rbb, int size);

/**
 * @brief Read the specified amount of bytes from the ringbuffer
 * @param rbb  : A valid pointer to a rbb_t structure
 * @param out  : A valid pointer initialized to store the read data 
 * @param size : The amount of bytes to read and copy to the memory
 *               pointed by 'out'
 * @return     : The amount of bytes actually read from the ringbuffer
 */
int rbb_read(rbb_t *rbb, u_char *out, int size);
/**
 * @brief Write the specified amount of bytes into the ringbuffer
 * @param rbb  : A valid pointer to a rbb_t structure
 * @param in   : A pointer to the data to copy into the ringbuffer
 * @param size : The amount of bytes to be copied
 * @return     : The amount of bytes actually copied into the ringbuffer
 * @note       : The ringbuffer may not fit the entire buffer to copy so 
 *               the returned value might be less than the input 'size'.
 *               The caller should check for the returned value and retry
 *               writing the remainder once the ringbuffer has been emptied
 *               sufficiently
 */
int rbb_write(rbb_t *rbb, u_char *in, int size);

/**
 * @brief Returns the amount of bytes available in the ringbuffer for reading
 * @param rbb  : A valid pointer to a rbb_t structure
 * @return the amount of bytes written into the ringbuffer and available for reading
 */
int rbb_len(rbb_t *rbb);

/**
 * @brief Scan the ringbuffer untill the specific byte is found
 * @param rbb   : A valid pointer to a rbb_t structure
 * @param octet : The byte to search into the ringbuffer
 * @return the offset to the specified byte, -1 if not found
 */
int rbb_find(rbb_t *rbb, u_char octet);

/**
 * @brief Read until a specific byte is found or maxsize is reached
 * @param rbb     : A valid pointer to a rbb_t structure
 * @param out     : A valid pointer initialized to store the read data 
 * @param maxsize : The maximum amount of bytes that can be copied to
 *                  the memory pointed by 'out'
 * @return        : The amount of bytes actually read from the ringbuffer
 */
int rbb_read_until(rbb_t *rbb, u_char octet, u_char *out, int maxsize);

/**
 * @brief Clear the ringbuffer by eventually skipping all the unread bytes (if any)
 * @param rbb : A valid pointer to a rbb_t structure
 */
void rbb_clear(rbb_t *rbb);

/**
 * @brief Release all resources associated to the rbb_t structure
 * @param rb : A valid pointer to a rbb_t structure
 */
void rbb_destroy(rbb_t *rbb);

#ifdef __cplusplus
}
#endif

#endif
