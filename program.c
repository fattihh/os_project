#include "program.h"    // Önce kendi header dosyanızı dahil edin

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

    // SIGCHLD sinyalini yakalamak için sinyal işleyicisini ayarla
    struct sigaction sa;
    sa.sa_handler = &handle_sigchld;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
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
int execute_external_with_input_redirection(char **args, char *input_file) {
    pid_t pid, wpid;
    int status;

    pid = fork();
    if (pid == 0) {
        // Çocuk süreç: giriş dosyasını stdin'e yönlendir
        int fd_in = open(input_file, O_RDONLY);
        if (fd_in < 0) {
            fprintf(stderr, "Giriş dosyası bulunamadı.\n");
            exit(EXIT_FAILURE);
        }
        if (dup2(fd_in, STDIN_FILENO) == -1) {
            perror("dup2");
            close(fd_in);
            exit(EXIT_FAILURE);
        }
        close(fd_in);

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

/**
 * Çıkış yönlendirmesi ile birlikte harici komutları çalıştıran fonksiyon
 */
int execute_external_with_output_redirection(char **args, char *output_file) {
    pid_t pid, wpid;
    int status;

    pid = fork();
    if (pid == 0) {
        // Çocuk süreç: çıkış dosyasını stdout'a yönlendir
        int fd_out = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd_out < 0) {
            perror("Çıkış dosyası açılamadı");
            exit(EXIT_FAILURE);
        }
        if (dup2(fd_out, STDOUT_FILENO) == -1) {
            perror("dup2");
            close(fd_out);
            exit(EXIT_FAILURE);
        }
        close(fd_out);

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

/**
 * Arka planda komutları çalıştıran fonksiyon
 */
int execute_external_background(char **args) {
    pid_t pid;
    int status;

    pid = fork();
    if (pid == 0) {
        // Çocuk süreç: komutu yürüt
        // Arka plan sürecinde terminali kontrol etmek istemiyorsanız, aşağıdaki satırı ekleyebilirsiniz:
        // setsid();
        if (execvp(args[0], args) == -1) {
            perror("osprojectsh");
        }
        exit(EXIT_FAILURE);
    } else if (pid < 0) {
        // Hata durumu
        perror("osprojectsh");
    } else {
        // Ebeveyn süreç: arka plan sürecini listeye ekle
        bg_process *new_bg = malloc(sizeof(bg_process));
        if (!new_bg) {
            fprintf(stderr, "osprojectsh: Bellek tahsisi başarısız\n");
            return 1;
        }
        new_bg->pid = pid;
        new_bg->next = bg_list;
        bg_list = new_bg;
        // Arka plan sürecinin başlatıldığını bildir
        printf("[%d] retval: 0\n", pid);
    }

    return 1;
}

/**
 * Arka plan süreçlerini yakalayan sinyal işleyicisi
 */
void handle_sigchld(int sig) {
    pid_t pid;
    int status;

    // Çocuk süreçlerin sonlanmasını kontrol et
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        // Arka plan sürecini listeden çıkar
        bg_process **current = &bg_list;
        while (*current) {
            if ((*current)->pid == pid) {
                bg_process *temp = *current;
                *current = (*current)->next;
                free(temp);
                break;
            }
            current = &((*current)->next);
        }

        // Exit kodunu al
        int exit_code = 0;
        if (WIFEXITED(status)) {
            exit_code = WEXITSTATUS(status);
        }

        // Kullanıcıya bildir
        printf("\n[%d] retval: %d\n", pid, exit_code);
        display_prompt();
        fflush(stdout);
    }
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
 * Giriş, Çıkış yönlendirmesi ve arka plan çalıştırma kontrolü eklenmiştir.
 */
int execute_command(char **args) {
    if (args[0] == NULL) {
        // Boş komut girilmiş
        return 1;
    }

    // Arka plan çalıştırma kontrolü
    int background = 0;
    int i;
    for (i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], "&") == 0) {
            background = 1;
            args[i] = NULL; // '&' karakterini kaldır
            break;
        }
    }

    // Giriş ve Çıkış yönlendirmesi kontrolü
    char *input_file = NULL;
    char *output_file = NULL;

    for (i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], "<") == 0) {
            if (args[i + 1] == NULL) {
                fprintf(stderr, "Giriş dosyası belirtilmedi.\n");
                return 1;
            }
            input_file = args[i + 1];
            args[i] = NULL; // args dizisini '<' öncesi ile sınırlamak için
            i++; // 'GirişDosyası' argümanını atlamak için
        } else if (strcmp(args[i], ">") == 0) {
            if (args[i + 1] == NULL) {
                fprintf(stderr, "Çıkış dosyası belirtilmedi.\n");
                return 1;
            }
            output_file = args[i + 1];
            args[i] = NULL; // args dizisini '>' öncesi ile sınırlamak için
            i++; // 'ÇıkışDosyası' argümanını atlamak için
        }
    }

    if (background) {
        return execute_external_background(args);
    }

    if (input_file != NULL && output_file != NULL) {
        // Hem giriş hem de çıkış yönlendirmesi mevcut
        pid_t pid, wpid;
        int status;

        pid = fork();
        if (pid == 0) {
            // Çocuk süreç

            // Giriş yönlendirmesi
            int fd_in = open(input_file, O_RDONLY);
            if (fd_in < 0) {
                fprintf(stderr, "Giriş dosyası bulunamadı.\n");
                exit(EXIT_FAILURE);
            }
            if (dup2(fd_in, STDIN_FILENO) == -1) {
                perror("dup2");
                close(fd_in);
                exit(EXIT_FAILURE);
            }
            close(fd_in);

            // Çıkış yönlendirmesi
            int fd_out = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd_out < 0) {
                perror("Çıkış dosyası açılamadı");
                exit(EXIT_FAILURE);
            }
            if (dup2(fd_out, STDOUT_FILENO) == -1) {
                perror("dup2");
                close(fd_out);
                exit(EXIT_FAILURE);
            }
            close(fd_out);

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
    } else if (input_file != NULL) {
        // Sadece giriş yönlendirmesi mevcut
        return execute_external_with_input_redirection(args, input_file);
    } else if (output_file != NULL) {
        // Sadece çıkış yönlendirmesi mevcut
        return execute_external_with_output_redirection(args, output_file);
    }

    // Yerleşik komutlar kontrolü
    for (int j = 0; j < num_builtins(); j++) {
        if (strcmp(args[0], builtin_commands[j]) == 0) {
            return (*builtin_functions[j])(args);
        }
    }

    // Yerleşik olmayan komutları çalıştır
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
