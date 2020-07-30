#include "const.h"
#include "transplant.h"
#include "debug.h"

#ifdef _STRING_H
#error "Do not #include <string.h>. You will get a ZERO."
#endif

#ifdef _STRINGS_H
#error "Do not #include <strings.h>. You will get a ZERO."
#endif

#ifdef _CTYPE_H
#error "Do not #include <ctype.h>. You will get a ZERO."
#endif

/*
 * You may modify this file and/or move the functions contained here
 * to other source files (except for main.c) as you wish.
 *
 * IMPORTANT: You MAY NOT use any array brackets (i.e. [ and ]) and
 * you MAY NOT declare any arrays or allocate any storage with malloc().
 * The purpose of this restriction is to force you to use pointers.
 * Variables to hold the pathname of the current file or directory
 * as well as other data have been pre-declared for you in const.h.
 * You must use those variables, rather than declaring your own.
 * IF YOU VIOLATE THIS RESTRICTION, YOU WILL GET A ZERO!
 *
 * IMPORTANT: You MAY NOT use floating point arithmetic or declare
 * any "float" or "double" variables.  IF YOU VIOLATE THIS RESTRICTION,
 * YOU WILL GET A ZERO!
 */


void ser_char_array(char *char_array, int include_null);
void ser_dir_entry_metadata(int entry_mode, long entry_size);
long char_array_length(char *char_array);
void ser_bytes(long x, int n);
void set_global_option(char c);
int is_set(char c);
void ser_header(int type, int depth, long size);
void ser_magic_sequence();
void set_entry_mode(int mode);
int check_magic_seq();
long desr_bytes(int n);
int validate_record_header(int rec_type, int depth, int received_type, int received_depth, long received_rec_size);
int read_header( int *received_type, int *received_depth, long *received_rec_size);
int makedir_recursively();

/*
 * A function that returns printable names for the record types, for use in
 * generating debugging printout.
 */
static char *record_type_name(int i) {
    switch(i) {
    case START_OF_TRANSMISSION:
	return "START_OF_TRANSMISSION";
    case END_OF_TRANSMISSION:
	return "END_OF_TRANSMISSION";
    case START_OF_DIRECTORY:
	return "START_OF_DIRECTORY";
    case END_OF_DIRECTORY:
	return "END_OF_DIRECTORY";
    case DIRECTORY_ENTRY:
	return "DIRECTORY_ENTRY";
    case FILE_DATA:
	return "FILE_DATA";
    default:
	return "UNKNOWN";
    }
}

/*
 * @brief  Initialize path_buf to a specified base path.
 * @details  This function copies its null-terminated argument string into
 * path_buf, including its terminating null byte.
 * The function fails if the argument string, including the terminating
 * null byte, is longer than the size of path_buf.  The path_length variable
 * is set to the length of the string in path_buf, not including the terminating
 * null byte.
 *
 * @param  Pathname to be copied into path_buf.
 * @return 0 on success, -1 in case of error
 */
int path_init(char *name) {
    char *name_pointer = name;
    char *path_buf_pointer = path_buf;
    int i=0;
    char prev;
    while( *(name_pointer) != '\0' ){
        if(i+1 > PATH_MAX){
            return -1;
        }
        while( (*(name_pointer) != '\0') && (*(name_pointer) == '/' && prev == '/')){
            name_pointer++;
            continue;
        }
        if(*(name_pointer) == '\0'){
            i--;
            break;
        }
        if(*(name_pointer) == '/' && *(name_pointer+1) == '\0'){
            i--;
            break;
        }

        *(path_buf_pointer) = *(name_pointer);
        prev = *(name_pointer);
        path_buf_pointer++;
        name_pointer++;
        i++;
    }
    path_buf_pointer--;
    while(*(path_buf_pointer) == '/'){
        path_buf_pointer--;
        path_length--;
    }
    path_buf_pointer++;
    *(path_buf_pointer) = '\0';
    path_length = i;
    return 0;
}

/*
 * @brief  Append an additional component to the end of the pathname in path_buf.
 * @details  This function assumes that path_buf has been initialized to a valid
 * string.  It appends to the existing string the path separator character '/',
 * followed by the string given as argument, including its terminating null byte.
 * The length of the new string, including the terminating null byte, must be
 * no more than the size of path_buf.  The variable path_length is updated to
 * remain consistent with the length of the string in path_buf.
 *
 * @param  The string to be appended to the path in path_buf.  The string must
 * not contain any occurrences of the path separator character '/'.
 * @return 0 in case of success, -1 otherwise.
 */
int path_push(char *name) {
    char *name_pointer = name;
    char *path_buf_pointer = path_buf + path_length;
    *(path_buf_pointer) = '/';
    path_buf_pointer++;
    path_length++;

    while( *(name_pointer) != '\0'){

        if((path_length + 1 ) > PATH_MAX ){
            return -1;
        }

        *(path_buf_pointer) = *(name_pointer);
        path_buf_pointer++;
        name_pointer++;
        path_length++;
    }
    *(path_buf_pointer) = '\0';
    return 0;
}

/*
 * @brief  Remove the last component from the end of the pathname.
 * @details  This function assumes that path_buf contains a non-empty string.
 * It removes the suffix of this string that starts at the last occurrence
 * of the path separator character '/'.  If there is no such occurrence,
 * then the entire string is removed, leaving an empty string in path_buf.
 * The variable path_length is updated to remain consistent with the length
 * of the string in path_buf.  The function fails if path_buf is originally
 * empty, so that there is no path component to be removed.
 *
 * @return 0 in case of success, -1 otherwise.
 */
int path_pop() {
    if(path_length == 0){
        return -1;
    }
    char *path_buf_pointer = path_buf + path_length -1;
    while(path_length > 0 && *(path_buf_pointer) != '/'){
        path_length--;
        path_buf_pointer--;
    }
    path_length--;
    *(path_buf_pointer) = '\0';
    return 0;
}

/*
 * @brief Deserialize directory contents into an existing directory.
 * @details  This function assumes that path_buf contains the name of an existing
 * directory.  It reads (from the standard input) a sequence of DIRECTORY_ENTRY
 * records bracketed by a START_OF_DIRECTORY and END_OF_DIRECTORY record at the
 * same depth and it recreates the entries, leaving the deserialized files and
 * directories within the directory named by path_buf.
 *
 * @param depth  The value of the depth field that is expected to be found in
 * each of the records processed.
 * @return 0 in case of success, -1 in case of an error.  A variety of errors
 * can occur, including depth fields in the records read that do not match the
 * expected value, the records to be processed to not being with START_OF_DIRECTORY
 * or end with END_OF_DIRECTORY, or an I/O error occurs either while reading
 * the records from the standard input or in creating deserialized files and
 * directories.
 */
int deserialize_directory(int depth) {
    int received_type;
    int received_depth;
    long received_rec_size;
    int res;
    int temp_res;
    res = read_header(&received_type, &received_depth, &received_rec_size);
    if(res == -1){
        return -1;
    }

    res = validate_record_header(START_OF_DIRECTORY, depth, received_type, received_depth, received_rec_size);
    if(res == -1){
        return -1;
    }

    res = read_header(&received_type, &received_depth, &received_rec_size);
    if(res == -1){
        return -1;
    }

    while( received_type != END_OF_DIRECTORY){
        int entry_mode = (int) desr_bytes(4);
        long entry_size = desr_bytes(8);
        long entry_name_length = received_rec_size - 12l - 16l;

        if(entry_name_length + 1 > NAME_MAX){
            return -1;
        }
        // printf("entry name to be deserialized: \n");
        int i = 0;
        while(entry_name_length>0){
            char c = getchar();
            // printf("%c ", o);
            *(name_buf+i) = c;
            entry_name_length--;
            i++;
        }
        *(name_buf+i) = '\0';
        path_push(name_buf);
        // printf("path_buf: %s\n",path_buf);
        struct stat file_stats;
        int entry_exist = stat(path_buf, &file_stats);

         if( (entry_mode & S_IFMT) == S_IFDIR ){
            // if(entry_exist == 0){
            //     if(!is_set('c')){
            //         return -1;
            //     }
            // }

            if(entry_exist != 0){
                mkdir(path_buf, 0700);
            }
            res = deserialize_directory(depth+1);

            if(res == -1){
                return -1;
            }
            set_entry_mode(entry_mode);
            path_pop();

         }else if( (entry_mode  & S_IFMT ) == S_IFREG){
            if(!is_set('c')){
                if(entry_exist == 0){
                    return -1;
                }
            }
            int res = deserialize_file(depth);
            if(res == -1){
                return -1;
            }
            set_entry_mode(entry_mode);
            path_pop();
         }else{
            return -1;
         }
        res = read_header(&received_type, &received_depth, &received_rec_size);

        if(res == -1){
            return -1;
        }
    }
     return 0;
}

/*
 * @brief Deserialize the contents of a single file.
 * @details  This function assumes that path_buf contains the name of a file
 * to be deserialized.  The file must not already exist, unless the ``clobber''
 * bit is set in the global_options variable.  It reads (from the standard input)
 * a single FILE_DATA record containing the file content and it recreates the file
 * from the content.
 *
 * @param depth  The value of the depth field that is expected to be found in
 * the FILE_DATA record.
 * @return 0 in case of success, -1 in case of an error.  A variety of errors
 * can occur, including a depth field in the FILE_DATA record that does not match
 * the expected value, the record read is not a FILE_DATA record, the file to
 * be created already exists, or an I/O error occurs either while reading
 * the FILE_DATA record from the standard input or while re-creating the
 * deserialized file.
 */
int deserialize_file(int depth){
    int received_type;
    int received_depth;
    long received_rec_size;
    int res;
    res = read_header(&received_type, &received_depth, &received_rec_size);
    if(res == -1){
        return -1;
    }

    res = validate_record_header(FILE_DATA, depth, received_type,received_depth,received_rec_size);
    if(res == -1){
        return -1;
    }

    FILE *file = fopen(path_buf, "w");

    if(file == NULL){
        return -1;
    }
    long remaning_bytes = received_rec_size - 16l;

    while(remaning_bytes > 0){
        char o = getchar();
        fputc(o, file);
        remaning_bytes--;
    }
    fclose(file);
    return 0;
}

/*
 * @brief  Serialize the contents of a directory as a sequence of records written
 * to the standard output.
 * @details  This function assumes that path_buf contains the name of an existing
 * directory to be serialized.  It serializes the contents of that directory as a
 * sequence of records that begins with a START_OF_DIRECTORY record, ends with an
 * END_OF_DIRECTORY record, and with the intervening records all of type DIRECTORY_ENTRY.
 *
 * @param depth  The value of the depth field that is expected to occur in the
 * START_OF_DIRECTORY, DIRECTORY_ENTRY, and END_OF_DIRECTORY records processed.
 * Note that this depth pertains only to the "top-level" records in the sequence:
 * DIRECTORY_ENTRY records may be recursively followed by similar sequence of
 * records describing sub-directories at a greater depth.
 * @return 0 in case of success, -1 otherwise.  A variety of errors can occur,
 * including failure to open files, failure to traverse directories, and I/O errors
 * that occur while reading file content and writing to standard output.
 */
int serialize_directory(int depth) {
    DIR *directory = opendir(path_buf);

    if(directory == NULL){
        return -1;
    }
    struct dirent * dir_entry;
    int res;
    long size_dir_entry;
    long entry_name_length;
    long entry_size;
    int entry_mode;

    while( (dir_entry = readdir(directory)) != NULL ){
        char *name = dir_entry->d_name;
        if(*name == '.' || (*(name) == '.' && *(name+1) == '.')){
            continue;
        }
        char type = dir_entry->d_type;
        path_push(name);
        struct stat file_stats;
        stat(path_buf, &file_stats);

        entry_mode = file_stats.st_mode;
        entry_size = file_stats.st_size;
        entry_name_length =  char_array_length(name);
        size_dir_entry = 16l + 12l  + entry_name_length;

        ser_header(DIRECTORY_ENTRY, depth, size_dir_entry);
        ser_dir_entry_metadata(entry_mode, entry_size);
        ser_char_array(name, 0);

        if((entry_mode & S_IFMT) == S_IFDIR){
            ser_header(START_OF_DIRECTORY, depth+1, 16l);
            res = serialize_directory(depth+1);

            if(res == 0){
                ser_header(END_OF_DIRECTORY, depth+1, 16l);
            }
        }else if((entry_mode  & S_IFMT ) == S_IFREG){
            off_t file_size = entry_size;
            res = serialize_file(depth, file_size);
        }
        if(res == 0){
            path_pop(name);
        }else{
            return -1;
        }
    }
    return 0;
}

/*
 * @brief  Serialize the contents of a file as a single record written to the
 * standard output.
 * @details  This function assumes that path_buf contains the name of an existing
 * file to be serialized.  It serializes the contents of that file as a single
 * FILE_DATA record emitted to the standard output.
 *
 * @param depth  The value to be used in the depth field of the FILE_DATA record.
 * @param size  The number of bytes of data in the file to be serialized.
 * @return 0 in case of success, -1 otherwise.  A variety of errors can occur,
 * including failure to open the file, too many or not enough data bytes read
 * from the file, and I/O errors reading the file data or writing to standard output.
 */
int serialize_file(int depth, off_t size) {
    ser_header(FILE_DATA, depth, ((long) size + 16l));
    FILE *file = fopen(path_buf, "r");

    if(file == NULL){
        return -1;
    }
    char c;

    long t = size;
    while( t != 0 ){
        c = fgetc(file);
        putchar(c);
        t--;
    }
    fclose(file);
    return 0;
}

/**
 * @brief Serializes a tree of files and directories, writes
 * serialized data to standard output.
 * @details This function assumes path_buf has been initialized with the pathname
 * of a directory whose contents are to be serialized.  It traverses the tree of
 * files and directories contained in this directory (not including the directory
 * itself) and it emits on the standard output a sequence of bytes from which the
 * tree can be reconstructed.  Options that modify the behavior are obtained from
 * the global_options variable.
 *
 * @return 0 if serialization completes without error, -1 if an error occurs.
 */
int serialize() {
    ser_header(START_OF_TRANSMISSION, 0, 16l);
    ser_header(START_OF_DIRECTORY, 1, 16l);
    int ret = serialize_directory(1);

    if(ret == 0){
        ser_header(END_OF_DIRECTORY, 1, 16l);
        ser_header(END_OF_TRANSMISSION, 0, 16l);
        return 0;
    }
    return -1;
}

/**
 * @brief Reads serialized data from the standard input and reconstructs from it
 * a tree of files and directories.
 * @details  This function assumes path_buf has been initialized with the pathname
 * of a directory into which a tree of files and directories is to be placed.
 * If the directory does not already exist, it is created.  The function then reads
 * from from the standard input a sequence of bytes that represent a serialized tree
 * of files and directories in the format written by serialize() and it reconstructs
 * the tree within the specified directory.  Options that modify the behavior are
 * obtained from the global_options variable.
 *
 * @return 0 if deserialization completes without error, -1 if an error occurs.
 */
int deserialize() {
    int res;
    struct stat file_stats;
    int entry_exist = stat(path_buf, &file_stats);
    if(entry_exist != 0 ){

        /*The only purpose of this block is to prevent the deserialize code
        from creating the new dir before the serialize process iterate over
        the original struct of pwd.
        This prevents the new dir from being listed in the serialized content.
        If the serialize process takes more time. the number 1000000 has to be
        incereased.
        */        

        res = makedir_recursively();
        if(res == -1){
            return -1;
        }
        // mkdir(path_buf,0755);
    }

    // else{
    //     if(!is_set('c')){
    //         printf("clobber not set for dir\n");
    //         return -1;
    //     }
    // }

    DIR *directory = opendir(path_buf);

    if(directory == NULL){
        return -1;
    }
    int received_type;
    int received_depth;
    long received_rec_size;

    res = read_header(&received_type, &received_depth, &received_rec_size);
    if(res == -1){
        return -1;
    }

    res = validate_record_header(START_OF_TRANSMISSION, 0, received_type, received_depth, received_rec_size);
    if(res == -1){
        return -1;
    }

    res = deserialize_directory(1);
    if(res == -1){
        return -1;
    }

    res = read_header(&received_type, &received_depth, &received_rec_size);
    if(res == -1){
        return -1;
    }

    res = validate_record_header(END_OF_TRANSMISSION, 0, received_type, received_depth, received_rec_size);
    if(res == -1){
        return -1;
    }
    return 0;
}

/**
 * @brief Validates command line arguments passed to the program.
 * @details This function will validate all the arguments passed to the
 * program, returning 0 if validation succeeds and -1 if validation fails.
 * Upon successful return, the selected program options will be set in the
 * global variable "global_options", where they will be accessible
 * elsewhere in the program.
 *
 * @param argc The number of arguments passed to the program from the CLI.
 * @param argv The argument strings passed to the program from the CLI.
 * @return 0 if validation succeeds and -1 if validation fails.
 * Refer to the homework document for the effects of this function on
 * global variables.
 * @modifies global variable "global_options" to contain a bitmap representing
 * the selected options.
 */
int validargs(int argc, char **argv){
    int arg_counter = 1;

    if(argc == 1){
        return -1;
    }

    while((arg_counter) < argc){
        char c0 = *(*(argv + arg_counter)+0);
        char c1 = *(*(argv + arg_counter)+1);
        char c2 = *(*(argv + arg_counter)+2);

        if(c0 == '-'){
            if(c2 != '\0' ){
                return -1;
            }

            if(!(c1 == 'h' || c1 == 's' || c1 == 'd' || c1 == 'p' || c1 == 'c')){
                return -1;
            }

            if((c1 == 'h' || c1 == 's' || c1 == 'd') && c2 == '\0'){
                set_global_option(c1);
                if(c1 == 'h') return 0;
            }
            else if(c1 == 'p'){
                if(is_set('s') == 0 && is_set('d') == 0){
                    return -1;
                }
                //Based on piazza discussion and discussion with prof. Assumption "-" will be considered for flags, dir name will not start with "-"
                if(arg_counter == argc -1){
                    return -1;
                }
                arg_counter++;
                char *path = *(argv + arg_counter);

                if(*(path) == '-'){
                    return -1;
                }
                int initialize_path =  path_init(path);

                if(initialize_path != 0){
                    return -1;
                }
            }else if(c1 == 'c'){
                if(is_set('d') == 0 || is_set('s') != 0){
                    return -1;
                }
                if(path_length == 0){
                    if(PATH_MAX > 0){
                        *(path_buf) = '.';
                        *(path_buf + 1) = '\0';
                        path_length = 1;
                    }else{
                        return -1;
                    }
                }
                set_global_option(c1);
            }

            if(arg_counter == argc -1){
                if(path_length == 0 && ( is_set('s') == 0 || is_set('d') == 0) ){
                    *(path_buf) = '.';
                    *(path_buf + 1) = '\0';
                    path_length = 1;
                }
            }
        }else{
            return -1;
        }
        arg_counter++;
    }
    return 0;
}
void ser_char_array(char *char_array, int include_null){
    char c;
    int i=0;
    while( (c = *(char_array+i)) != '\0'){
        putchar(c);
        i++;
    }
    if(include_null != 0){
        putchar(c);
    }
}
void ser_dir_entry_metadata(int entry_mode, long entry_size){
    ser_bytes((long) entry_mode, 4);
    ser_bytes(entry_size ,8);
}
long char_array_length(char *char_array){
    char c;
    int i=0;
    while( (c = *(char_array+i)) != '\0'){
        i++;
    }
    return i;
}
void ser_bytes(long x, int n){
    for(int i = ((n-1)*8); i >= 0; i = i-8){
        putchar((x >> i) & 0xff);
    }
}

void set_global_option(char c){
    if(c == 'h'){
        global_options |= (global_options & 0xff) | (1);
    }else if(c == 's'){
        global_options |= ((global_options & 0xff) |  (1 << 1) );
    }else if(c == 'd'){
        global_options |= ((global_options & 0xff) |  (1 << 2) );
    }else{
        global_options |= ((global_options & 0xff) |  (1 << 3) );
    }
}

int is_set(char c){
    if(c == 's'){
        return ( global_options &  (1 << 1) );
    } else if( c == 'd'){
        return (global_options &  (1 << 2) );
    }else if( c == 'c'){
        return (global_options &  (1 << 3) );
    }else return -1;
}
void ser_header(int type, int depth, long size){
    ser_magic_sequence();
    ser_bytes((long) type, 1);
    ser_bytes((long) depth, 4);
    ser_bytes( size, 8);
}
void ser_magic_sequence(){
    int c = 0x0c;
    putchar(c);
    c = 0x0d;
    putchar(c);
     c = 0xed;
    putchar(c);
}
void set_entry_mode(int mode){
    chmod(path_buf, mode & 0777);
    return;
}
int check_magic_seq(){
    int o = getchar();

    if(o != 0x0c){
        return -1;
    }
    o = getchar();
    if(o != 0x0d){
        return -1;
    }
    o = getchar();
    if(o != 0xed){
        return -1;
    }
    return 0;
}
long desr_bytes(int n){
    long value = 0l;
    for(int i = ((n-1)*8); i >= 0; i=i-8){
        int c;
        c = getchar();
        value |= (c << i);
    }
    return value;
}

int read_header( int *received_type, int *received_depth, long *received_rec_size){
    if(check_magic_seq()){
        return -1;
    }
    *received_type = getchar();
    *received_depth = (int) desr_bytes(4);
    *received_rec_size = desr_bytes(8);
    return 0;
}

int validate_record_header(int rec_type, int depth, int received_type, int received_depth, long received_rec_size){
    if(rec_type != received_type){
        return -1;
    }

    if( depth != received_depth){
        return -1;
    }

    if( received_type == START_OF_DIRECTORY || received_type == END_OF_DIRECTORY || received_type == START_OF_TRANSMISSION || received_type == END_OF_TRANSMISSION ){
        if(received_rec_size != 0x10){
            return -1;
        }
    }
    return 0;
}

int makedir_recursively(){
    int i=0;
    int res;
    while(i <= path_length-1){
        while((i <= path_length-1) && (*(path_buf+i) != '/')){
            i++;
        }
        *(path_buf+i) = '\0';
        struct stat file_stats;
        int entry_exist = stat(path_buf, &file_stats);
        if(entry_exist !=  0){
            res = mkdir(path_buf, 0700);
            if(res != 0){
                return -1;
            }
        }else{
            *(path_buf+i) = '/';
            break;
        }
        if(path_length == i){
            break;
        }
        *(path_buf+i) = '/';
        i++;
    }
    return 0;
}
