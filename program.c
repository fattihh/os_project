#include "program.h"

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
    char hostn[256] = "";
    gethostname(hostn, sizeof(hostn));
    if (getcwd(currentDirectory, 1024) == NULL) {
        perror("osprojectsh");
        strcpy(currentDirectory, "?");
    }
    printf(KNRM "%s@%s:" KWHT KBLU "%s > " KWHT, getenv("USER"), hostn, currentDirectory);
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
    printf("Yerleşik Komutlar:\n");
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
 * Komut satırını borulara göre bölen fonksiyon.
 * @param line Komut satırı girişi.
 * @param num_commands Komut sayısını döndürmek için kullanılan çıktı parametresi.
 * @return Komutların dizi dizisi.
 */
char ***split_commands(char *line, int *num_commands) {
    int bufsize = 10;
    int position = 0;
    char ***commands = malloc(bufsize * sizeof(char**));
    char *command;
    
    if (!commands) {
        fprintf(stderr, "osprojectsh: Bellek tahsisi başarısız\n");
        exit(EXIT_FAILURE);
    }
    
    command = strtok(line, "|");
    while (command != NULL) {
        // Her komut için baştaki boşlukları temizle
        while (*command == ' ') command++;
        
        // Komutu tokenlara ayır
        commands[position++] = split_line(command);
        
        if (position >= bufsize) {
            bufsize += 10;
            commands = realloc(commands, bufsize * sizeof(char**));
            if (!commands) {
                fprintf(stderr, "osprojectsh: Bellek tahsisi başarısız\n");
                exit(EXIT_FAILURE);
            }
        }
        
        command = strtok(NULL, "|");
    }
    commands[position] = NULL;
    *num_commands = position;

    // Debug: Bölünmüş komutları yazdır
    for (int i = 0; i < *num_commands; i++) {
        printf("Split Command[%d]: '%s'\n", i+1, commands[i][0]);
    }

    return commands;
}

/**
 * Boru (pipe) içeren komut satırlarını çalıştıran fonksiyon.
 * @param commands Borulara ayrılmış komutların dizisi.
 * @param num_commands Komut sayısı.
 * @return 1 Her zaman başarılı olarak döner.
 */
int execute_piped_commands(char ***commands, int num_commands) {
    int i;
    pid_t pid;
    int in_fd = 0; // İlk komut için standart giriş
    int fd[2];
    pid_t *pids = malloc(num_commands * sizeof(pid_t));

    if (!pids) {
        fprintf(stderr, "osprojectsh: Bellek tahsisi başarısız\n");
        return 1;
    }

    for (i = 0; i < num_commands; i++) {
        if (i < num_commands - 1) {
            // Her komut için bir pipe oluştur
            if (pipe(fd) == -1) {
                perror("pipe");
                free(pids);
                return 1;
            }
        }

        pid = fork();
        if (pid == 0) {
            // Çocuk süreç

            // Giriş yönlendirmesi
            if (in_fd != 0) {
                if (dup2(in_fd, STDIN_FILENO) == -1) {
                    perror("dup2");
                    exit(EXIT_FAILURE);
                }
                close(in_fd);
            }

            // Çıkış yönlendirmesi
            if (i < num_commands - 1) {
                close(fd[0]); // Okuma ucunu kapat
                if (dup2(fd[1], STDOUT_FILENO) == -1) {
                    perror("dup2");
                    exit(EXIT_FAILURE);
                }
                close(fd[1]);
            }

            // Debug: Komut ve Argümanları Yazdırma
            printf("Executing command %d: %s\n", i + 1, commands[i][0]);
            for (int j = 0; commands[i][j] != NULL; j++) {
                printf("  args[%d]: %s\n", j, commands[i][j]);
            }

            // Komutu yürüt
            if (execvp(commands[i][0], commands[i]) == -1) {
                perror("execvp");
                exit(EXIT_FAILURE);
            }
        } else if (pid < 0) {
            // Fork hatası
            perror("fork");
            free(pids);
            return 1;
        } else {
            // Ebeveyn süreç
            pids[i] = pid;

            // Önceki giriş dosyasını kapat
            if (in_fd != 0) {
                close(in_fd);
            }

            // Çıkış dosyasını ebeveyn sürece aktar
            if (i < num_commands - 1) {
                close(fd[1]); // Yazma ucunu kapat
                in_fd = fd[0]; // Sonraki komutun girişi için
            }
        }
    }

    // Ebeveyn süreç tüm çocuk süreçlerin bitmesini bekler
    for (i = 0; i < num_commands; i++) {
        int status;
        waitpid(pids[i], &status, 0);
        // Çıkış kodlarını burada işleyebilirsiniz
    }

    free(pids);
    return 1;
}

int execute_external(char **args) {
    pid_t pid, wpid;
    int status;

    pid = fork();
    if (pid == 0) {
        // Çocuk süreç: komutu yürüt
        if (execvp(args[0], args) == -1) {
            perror("execvp");
        }
        exit(EXIT_FAILURE);
    } else if (pid < 0) {
        // Hata durumu
        perror("fork");
    } else {
        // Ebeveyn süreç: çocuğun bitmesini bekle
        do {
            wpid = waitpid(pid, &status, WUNTRACED);
        } while (!WIFEXITED(status) && !WIFSIGNALED(status));
    }

    return 1;
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
            perror("execvp");
        }
        exit(EXIT_FAILURE);
    } else if (pid < 0) {
        // Hata durumu
        perror("fork");
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
            perror("open");
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
            perror("execvp");
        }
        exit(EXIT_FAILURE);
    } else if (pid < 0) {
        // Hata durumu
        perror("fork");
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
            perror("execvp");
        }
        exit(EXIT_FAILURE);
    } else if (pid < 0) {
        // Hata durumu
        perror("fork");
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
 * Boru karakteri içerip içermediğini kontrol edip uygun şekilde çalıştıran fonksiyon.
 * @param args Komut argümanları dizisi.
 * @param line Orijinal komut satırı girişi.
 * @return 1 Her zaman başarılı olarak döner.
 */
int execute_command(char **args, char *line) {
    // Boru karakteri içerip içermediğini kontrol et
    int i;
    int pipe_found = 0;
    for (i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], "|") == 0) {
            pipe_found = 1;
            break;
        }
    }

    if (pipe_found) {
        // Boru bulundu, komut satırını borulara göre böl
        // Orijinal komut satırını yeniden birleştirmek yerine, doğrudan borulara göre böl
        int num_commands = 0;
        // strdup ile orijinal line'ı kopyalayın çünkü strtok line'ı değiştirecek
        char *line_copy = strdup(line);
        if (!line_copy) {
            fprintf(stderr, "osprojectsh: Bellek tahsisi başarısız\n");
            return 1;
        }
        char ***commands = split_commands(line_copy, &num_commands);

        // Boru içeren komutları çalıştır
        int status = execute_piped_commands(commands, num_commands);

        // Belleği temizle
        for (i = 0; i < num_commands; i++) {
            free(commands[i]);
        }
        free(commands);
        free(line_copy);

        return status;
    }

    // Boru yok, normal şekilde çalıştır
    // Arka plan çalıştırma kontrolü
    int background = 0;
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
                perror("open");
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
                perror("execvp");
            }
            exit(EXIT_FAILURE);
        } else if (pid < 0) {
            // Hata durumu
            perror("fork");
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
        status = execute_command(args, line);

        // Belleği temizle
        free(args);

    } while (status);

    // Belleği serbest bırak
    free(line);
    free(currentDirectory);

    return EXIT_SUCCESS;
}
