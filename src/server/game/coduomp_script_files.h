#ifndef CODUOMP_SCRIPT_FILES_H
#define CODUOMP_SCRIPT_FILES_H

/* NOT_FROM_ORIGINAL_SOURCE: coduox: fs_* script file functions. */
void coduomp_script_func_fs_testfile(void);
void coduomp_script_func_fs_fopen(void);
void coduomp_script_func_fs_readline(void);
void coduomp_script_func_fs_writeline(void);
void coduomp_script_func_fs_remove(void);
void coduomp_script_func_fs_fclose(void);
void coduomp_script_func_fs_fcloseall(void);

/* NOT_FROM_ORIGINAL_SOURCE: coduox: closes every open script file handle;
 * called when the game shuts down or restarts so handles never outlive their
 * scripts. */
void coduomp_script_files_close_all(void);

#endif
