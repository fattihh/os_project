#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include "program.h"  // Kendi header dosyanız

#define BUFFER_SIZE 1024
#define TOK_DELIM " \t\r\n\a"

// Global değişkenler
char* currentDirectory;
bg_process *bg_list = NULL;

// Yerleşik komutlar ve fonksiyonları
char *builtin_commands[] = {
    "cd",
    "help",
    "quit"
};

int (*builtin_functions[])(char**) = {
    &shell_cd,
    &shell_help,
    &shell_quit
};

int num_builtins() {
    return sizeof(builtin_commands) / sizeof(char *);
}

/**
Komut satırı prompt'unu gösteren fonksiyon
*/
void display_prompt() {
    char hostn[1204] = "";
    gethostname(hostn, sizeof(hostn));
    if (getcwd(currentDirectory, 1024) == NULL) {
        perror("osprojectsh");
        strcpy(currentDirectory, "?");
    }
    printf(KNRM "%s@%s:" KWHT KBLU "%s > " KWHT, getenv("LOGNAME"), hostn, currentDirectory);
}

/**
Programın başlangıç işlemlerini gerçekleştiren fonksiyon
*/
void initialize_shell() {
    int i;
    for (i = 0; i < 40; i++)
        printf("%s", "=");
    printf("\n");
    print_spaces();
    printf("%s%s%s\n", "=", KCYN "              OS PROJECT SHELL               ", KWHT "=");
    print_spaces();
    for (i = 0; i < 40; i++)
        printf("%s", "=");
    printf("\n");
}

/**
cd komutunu gerçekleştiren fonksiyon
*/
int shell_cd(char **args) {
    if (args[1] == NULL) {
        fprintf(stderr, "osprojectsh: \"cd\" komutu için argüman bekleniyor\n");
    } else {
        if (chdir(args[1]) != 0) {
            perror("osprojectsh");
        }
    }
    return 1;
}

/**
Yardım komutunu gerçekleştiren fonksiyon
*/
int shell_help(char **args) {
    int i;
    printf("İşletim Sistemleri Ödevi - OS Project Shell\n");
    printf("%s\n", "Yerleşik Komutlar:");
    for (i = 0; i < num_builtins(); i++) {
        printf("  %s\n", builtin_commands[i]);
    }
    printf("Diğer programlar için 'man' komutunu kullanarak yardım alabilirsiniz.\n");
    return 1;
}

/**
quit komutunu gerçekleştiren fonksiyon
*/
int shell_quit(char **args) {
    int status;
    // Arka planda çalışan tüm süreçleri bekle
    bg_process *curr = bg_list;
    while (curr != NULL) {
        waitpid(curr->pid, &status, 0);
        printf("[%d] retval: %d\n", curr->pid, WEXITSTATUS(status));
        bg_process *temp = curr;
        curr = curr->next;
        free(temp);
    }
    exit(0);
}

// Yardımcı fonksiyonlar
char **split_line(char *line) {
    int bufsize = 64, position = 0;
    char **tokens = malloc(bufsize * sizeof(char*));
    char *token;

    if (!tokens) {
        fprintf(stderr, "osprojectsh: Bellek tahsisi başarısız\n");
        exit(EXIT_FAILURE);
    }

    token = strtok(line, TOK_DELIM);
    while (token != NULL) {
        tokens[position++] = token;

        if (position >= bufsize) {
            bufsize += 64;
            tokens = realloc(tokens, bufsize * sizeof(char*));
            if (!tokens) {
                fprintf(stderr, "osprojectsh: Bellek tahsisi başarısız\n");
                exit(EXIT_FAILURE);
            }
        }

        token = strtok(NULL, TOK_DELIM);
    }
    tokens[position] = NULL;
    return tokens;
}

/**
 * Giriş yönlendirmesi ile birlikte harici komutları çalıştıran fonksiyon
 */
int execute_external_with_redirection(char **args, char *input_file) {
    pid_t pid, wpid;
    int status;

    // Giriş dosyasını aç
    FILE *file = fopen(input_file, "r");
    if (file == NULL) {
        fprintf(stderr, "Giriş dosyası bulunamadı.\n");
        return 1;
    }

    pid = fork();
    if (pid == 0) {
        // Çocuk süreç: giriş dosyasını stdin'e yönlendir
        if (dup2(fileno(file), STDIN_FILENO) == -1) {
            perror("dup2");
            fclose(file);
            exit(EXIT_FAILURE);
        }
        fclose(file);

        // Komutu yürüt
        if (execvp(args[0], args) == -1) {
            perror("osprojectsh");
        }
        exit(EXIT_FAILURE);
    } else if (pid < 0) {
        // Hata durumu
        perror("osprojectsh");
    } else {
        // Ebeveyn süreç: çocuğun bitmesini bekle
        do {
            wpid = waitpid(pid, &status, WUNTRACED);
        } while (!WIFEXITED(status) && !WIFSIGNALED(status));
    }

    return 1;
}

int execute_external(char **args) {
    pid_t pid, wpid;
    int status;

    pid = fork();
    if (pid == 0) {
        // Çocuk süreç: komutu yürüt
        if (execvp(args[0], args) == -1) {
            perror("osprojectsh");
        }
        exit(EXIT_FAILURE);
    } else if (pid < 0) {
        // Hata durumu
        perror("osprojectsh");
    } else {
        // Ebeveyn süreç: çocuğun bitmesini bekle
        do {
            wpid = waitpid(pid, &status, WUNTRACED);
        } while (!WIFEXITED(status) && !WIFSIGNALED(status));
    }

    return 1;
}

/**
 * Girilen komutu analiz eder ve uygun şekilde çalıştırır.
 * Giriş yönlendirmesi kontrolü eklenmiştir.
 */
int execute_command(char **args) {
    if (args[0] == NULL) {
        // Boş komut girilmiş
        return 1;
    }

    // Giriş yönlendirmesi kontrolü
    int i;
    char *input_file = NULL;
    for (i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], "<") == 0) {
            if (args[i + 1] == NULL) {
                fprintf(stderr, "Giriş dosyası belirtilmedi.\n");
                return 1;
            }
            input_file = args[i + 1];
            args[i] = NULL; // args dizisini '<' öncesi ile sınırlamak için
            break;
        }
    }

    if (input_file != NULL) {
        return execute_external_with_redirection(args, input_file);
    }

    for (int i = 0; i < num_builtins(); i++) {
        if (strcmp(args[0], builtin_commands[i]) == 0) {
            return (*builtin_functions[i])(args);
        }
    }

    return execute_external(args);
}

/**
 * Ekranda belirli sayıda boşluk veya hizalama sağlayan fonksiyon
 */
void print_spaces() {
    // 10 boşluk yazdır ve satır atla
    for (int i = 0; i < 10; i++) {
        printf(" ");
    }
    printf("\n");
}

int main(int argc, char **argv) {
    char *line = NULL;
    char **args;
    int status;

    // currentDirectory için bellek ayırın
    currentDirectory = malloc(1024 * sizeof(char));
    if (currentDirectory == NULL) {
        fprintf(stderr, "osprojectsh: Bellek tahsisi başarısız\n");
        exit(EXIT_FAILURE);
    }

    // Kabuk başlatma
    initialize_shell();

    // Ana döngü
    do {
        display_prompt();

        // Kullanıcı girdisini oku
        ssize_t bufsize = 0;
        if (getline(&line, &bufsize, stdin) == -1) {
            if (feof(stdin)) {
                // Ctrl+D ile çıkış
                printf("\n");
                break;
            } else {
                perror("osprojectsh: getline");
                continue;
            }
        }

        // Girdiyi parçalara ayır
        args = split_line(line);

        // Komutu yürüt
        status = execute_command(args);

        // Belleği temizle
        free(args);

    } while (status);

    // Belleği serbest bırak
    free(line);
    free(currentDirectory);

    return EXIT_SUCCESS;
}
