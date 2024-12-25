#ifndef PROGRAM_H
#define PROGRAM_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h> // Çıkış dosyası açma için ekledi

// Renk Kodları
#define KNRM  "\x1B[0m"   // Normal
#define KWHT  "\x1B[37m"  // Beyaz
#define KBLU  "\x1B[34m"  // Mavi
#define KCYN  "\x1B[36m"  // Camgöbeği

// Arka Plan Süreç Yapısı
typedef struct bg_process {
    pid_t pid;                  // Süreç ID'si
    struct bg_process *next;    // Sonraki süreç
} bg_process;

// Global Değişkenler
extern char* currentDirectory;     // Geçerli Dizin
extern bg_process *bg_list;        // Arka planda çalışan süreçler listesi

// Yerleşik Komutlar ve Fonksiyonları
extern char *builtin_commands[];                        // Yerleşik komutlar dizisi
extern int (*builtin_functions[])(char**);              // Yerleşik komut fonksiyonları dizisi

// Fonksiyon Prototipleri

// Yerleşik komut sayısını döndüren fonksiyon
int num_builtins();

// Komut satırı prompt'unu gösteren fonksiyon
void display_prompt();

// Programın başlangıç işlemlerini gerçekleştiren fonksiyon
void initialize_shell();

// Yerleşik komut fonksiyonları
int shell_cd(char **args);
int shell_help(char **args);
int shell_quit(char **args);

// Yardımcı Fonksiyonlar
char **split_line(char *line); // Kullanıcı girdisini tokenlara ayırır.
int execute_external(char **args); // Yerleşik olmayan komutları harici olarak çalıştırır.
int execute_command(char **args); // Girilen komutu analiz eder ve uygun şekilde çalıştırır.

// Giriş ve Çıkış Yönlendirme Fonksiyonları
int execute_external_with_input_redirection(char **args, char *input_file);
int execute_external_with_output_redirection(char **args, char *output_file);

// Diğer Yardımcı Fonksiyonlar
void print_spaces();

#endif // PROGRAM_H
