#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>
#include <readline/readline.h>
#include <readline/history.h>

#define MAX_COMMAND_LENGTH 1024
#define MAX_ARGS 64
#define MAX_PROMPT_LENGTH 256
#define MAX_PRESETS 50
#define CONFIG_DIR ".sheller"
#define PRESETS_FILE "prompts"
#define LAST_PROMPT_FILE "last_prompt"
#define HISTORY_FILE "history"

typedef struct {
    char name[64];
    char prompt[MAX_PROMPT_LENGTH];
} PromptPreset;

// Global variables for presets
PromptPreset presets[MAX_PRESETS];
int preset_count = 0;

// Function prototypes
void print_prompt(const char* prompt);
char* read_command(const char* prompt);
char** parse_command(char* command, int* arg_count);
int execute_command(char** args, int arg_count, char** prompt);
int handle_builtin_commands(char** args, char** prompt);
void load_presets(void);
void save_presets(void);
char* parse_quoted_string(char* input);
void list_presets(void);
void ensure_config_dir(void);
char* get_config_path(const char* filename);
void save_last_prompt(const char* prompt);
char* load_last_prompt(void);
void initialize_readline(void);
void cleanup_readline(void);

int main(int argc, char* argv[]) {
    char* command;
    char** args;
    int arg_count;
    int status = 1;

    // check if directory exists
    ensure_config_dir();
    
    // Initialize the readline and load history
    initialize_readline();

    // Load saved presets
    load_presets();

    // Try to load last used prompt, goes to default if it doesnt exists
    char* prompt = load_last_prompt();
    if (!prompt) {
        prompt = strdup("愛> ");
    }

    while (status) {
        command = read_command(prompt);
        
        if (command == NULL) {
            continue;
        }

        // Add command to history only if it's not empty
        if (strlen(command) > 0) {
            add_history(command);
        }

        args = parse_command(command, &arg_count);
        if (args != NULL) {
            status = execute_command(args, arg_count, &prompt);
            // Save the current prompt after each change
            save_last_prompt(prompt);
            // Free the argument array
            for (int i = 0; i < arg_count; i++) {
                free(args[i]);
            }
            free(args);
        }
        free(command);
    }

    cleanup_readline();
    free(prompt);
    return 0;
}

void initialize_readline(void) {
    // Load history from file
    char* history_path = get_config_path(HISTORY_FILE);
    if (history_path) {
        read_history(history_path);
        free(history_path);
    }

    // Set readline to auto-complete filenames
    rl_bind_key('\t', rl_complete);
}

void cleanup_readline(void) {
    // Save history to file
    char* history_path = get_config_path(HISTORY_FILE);
    if (history_path) {
        write_history(history_path);
        free(history_path);
    }
}

char* read_command(const char* prompt) {
    char* line = readline(prompt);
    if (line && *line) {
        return line;
    }
    return line;
}

void ensure_config_dir(void) {
    char* home = getenv("HOME");
    if (!home) {
        fprintf(stderr, "HOME environment variable not set\n");
        return;
    }

    char config_path[1024];
    snprintf(config_path, sizeof(config_path), "%s/%s", home, CONFIG_DIR);

    struct stat st = {0};
    if (stat(config_path, &st) == -1) {
        if (mkdir(config_path, 0700) == -1) {
            perror("Failed to create config directory");
        }
    }
}

char* get_config_path(const char* filename) {
    char* home = getenv("HOME");
    if (!home) return NULL;

    char* path = malloc(1024);
    if (!path) return NULL;

    snprintf(path, 1024, "%s/%s/%s", home, CONFIG_DIR, filename);
    return path;
}

void save_last_prompt(const char* prompt) {
    char* config_path = get_config_path(LAST_PROMPT_FILE);
    if (!config_path) return;

    FILE* fp = fopen(config_path, "w");
    if (fp) {
        fprintf(fp, "%s", prompt);
        fclose(fp);
    }
    free(config_path);
}

char* load_last_prompt(void) {
    char* config_path = get_config_path(LAST_PROMPT_FILE);
    if (!config_path) return NULL;

    FILE* fp = fopen(config_path, "r");
    if (!fp) {
        free(config_path);
        return NULL;
    }

    char* prompt = malloc(MAX_PROMPT_LENGTH);
    if (!prompt) {
        fclose(fp);
        free(config_path);
        return NULL;
    }

    if (fgets(prompt, MAX_PROMPT_LENGTH, fp)) {
        // Remove trailing newline if present
        size_t len = strlen(prompt);
        if (len > 0 && prompt[len-1] == '\n') {
            prompt[len-1] = '\0';
        }
    } else {
        free(prompt);
        prompt = NULL;
    }

    fclose(fp);
    free(config_path);
    return prompt;
}

void load_presets(void) {
    char* config_path = get_config_path(PRESETS_FILE);
    if (!config_path) return;
    
    FILE* fp = fopen(config_path, "r");
    if (!fp) {
        free(config_path);
        return;
    }

    char line[MAX_PROMPT_LENGTH + 64];
    while (fgets(line, sizeof(line), fp) && preset_count < MAX_PRESETS) {
        char* name = strtok(line, "=");
        char* prompt = strtok(NULL, "\n");
        if (name && prompt) {
            strncpy(presets[preset_count].name, name, 63);
            strncpy(presets[preset_count].prompt, prompt, MAX_PROMPT_LENGTH - 1);
            preset_count++;
        }
    }
    fclose(fp);
    free(config_path);
}

void save_presets(void) {
    char* config_path = get_config_path(PRESETS_FILE);
    if (!config_path) return;
    
    FILE* fp = fopen(config_path, "w");
    if (!fp) {
        perror("Failed to save presets");
        free(config_path);
        return;
    }

    for (int i = 0; i < preset_count; i++) {
        fprintf(fp, "%s=%s\n", presets[i].name, presets[i].prompt);
    }
    fclose(fp);
    free(config_path);
}

char* parse_quoted_string(char* input) {
    char* result = malloc(strlen(input) + 1);
    char* write = result;
    int in_quotes = 0;
    
    while (*input) {
        if (*input == '"') {
            in_quotes = !in_quotes;
            input++;
            continue;
        }
        if (!in_quotes && isspace(*input)) {
            break;
        }
        *write++ = *input++;
    }
    *write = '\0';
    return result;
}

void list_presets(void) {
    printf("Available prompt presets:\n");
    for (int i = 0; i < preset_count; i++) {
        printf("%d. %s: %s\n", i + 1, presets[i].name, presets[i].prompt);
    }
}

void print_prompt(const char* prompt) {
    fputs(prompt, stdout);
    fflush(stdout);
}

int handle_builtin_commands(char** args, char** prompt) {
    if (strcmp(args[0], "exit") == 0) {
        return 0;
    } else if (strcmp(args[0], "cd") == 0) {
        if (args[1] == NULL) {
            fprintf(stderr, "cd: missing argument\n");
        } else if (chdir(args[1]) != 0) {
            perror("cd error");
        }
        return 1;
    } else if (strcmp(args[0], "prompt") == 0) {
        if (args[1] == NULL) {
            fprintf(stderr, "Usage:\n"
                          "  prompt \"your prompt\"       - Set current prompt\n"
                          "  prompt save name \"prompt\"  - Save preset\n"
                          "  prompt load name          - Load preset\n"
                          "  prompt delete name        - Delete preset\n"
                          "  prompt list               - List presets\n");
            return 1;
        }
        
        if (strcmp(args[1], "save") == 0) {
            if (args[2] == NULL || args[3] == NULL) {
                fprintf(stderr, "Usage: prompt save name \"prompt\"\n");
                return 1;
            }
            // Check if preset name already exists
            for (int i = 0; i < preset_count; i++) {
                if (strcmp(presets[i].name, args[2]) == 0) {
                    fprintf(stderr, "Preset '%s' already exists. Delete it first or use a different name.\n", args[2]);
                    return 1;
                }
            }
            if (preset_count >= MAX_PRESETS) {
                fprintf(stderr, "Maximum number of presets reached\n");
                return 1;
            }
            strncpy(presets[preset_count].name, args[2], 63);
            char* new_prompt = parse_quoted_string(args[3]);
            strncpy(presets[preset_count].prompt, new_prompt, MAX_PROMPT_LENGTH - 1);
            free(new_prompt);
            preset_count++;
            save_presets();
            printf("Preset '%s' saved\n", args[2]);
            return 1;
        }
        
        if (strcmp(args[1], "delete") == 0) {
            if (args[2] == NULL) {
                fprintf(stderr, "Usage: prompt delete name\n");
                return 1;
            }
            int found = 0;
            for (int i = 0; i < preset_count; i++) {
                if (strcmp(presets[i].name, args[2]) == 0) {
                    // Moves all subsequent presets up one position
                    for (int j = i; j < preset_count - 1; j++) {
                        strcpy(presets[j].name, presets[j + 1].name);
                        strcpy(presets[j].prompt, presets[j + 1].prompt);
                    }
                    preset_count--;
                    found = 1;
                    save_presets();
                    printf("Preset '%s' deleted\n", args[2]);
                    break;
                }
            }
            if (!found) {
                fprintf(stderr, "Preset '%s' not found\n", args[2]);
            }
            return 1;
        }
        
        if (strcmp(args[1], "load") == 0) {
            if (args[2] == NULL) {
                fprintf(stderr, "Usage: prompt load name\n");
                return 1;
            }
            for (int i = 0; i < preset_count; i++) {
                if (strcmp(presets[i].name, args[2]) == 0) {
                    free(*prompt);
                    *prompt = strdup(presets[i].prompt);
                    save_last_prompt(*prompt);  // Saves the newly loaded prompt
                    return 1;
                }
            }
            fprintf(stderr, "Preset '%s' not found\n", args[2]);
            return 1;
        }
        
        if (strcmp(args[1], "list") == 0) {
            if (preset_count == 0) {
                printf("No saved presets\n");
            } else {
                list_presets();
            }
            return 1;
        }

        char* new_prompt = parse_quoted_string(args[1]);
        free(*prompt);
        *prompt = new_prompt;
        
        // Add a space at the end if not there
        if ((*prompt)[strlen(*prompt) - 1] != ' ') {
            char* spaced_prompt = malloc(strlen(*prompt) + 3);
            if (spaced_prompt) {
                sprintf(spaced_prompt, "%s > ", *prompt);
                free(*prompt);
                *prompt = spaced_prompt;
            }
        }
        save_last_prompt(*prompt);
        return 1;
    }
    return -1;
}

char** parse_command(char* command, int* arg_count) {
    char** args = malloc(MAX_ARGS * sizeof(char*));
    char* token;
    *arg_count = 0;

    if (!args) {
        perror("malloc error");
        return NULL;
    }

    token = strtok(command, " ");
    while (token != NULL && *arg_count < MAX_ARGS - 1) {
        args[*arg_count] = strdup(token);
        if (!args[*arg_count]) {
            perror("strdup error");
            // Free previously allocated memory
            for (int i = 0; i < *arg_count; i++) {
                free(args[i]);
            }
            free(args);
            return NULL;
        }
        (*arg_count)++;
        token = strtok(NULL, " ");
    }
    args[*arg_count] = NULL;

    return args;
}

int execute_command(char** args, int arg_count, char** prompt) {
    if (arg_count == 0) return 1;

    // Check for built-in commands
    int builtin_result = handle_builtin_commands(args, prompt);
    if (builtin_result >= 0) return builtin_result;

    pid_t pid = fork();

    if (pid == 0) {
        // Child process
        if (execvp(args[0], args) == -1) {
            perror("Command execution failed");
            exit(EXIT_FAILURE);
        }
    } else if (pid < 0) {
        // Fork error
        perror("Fork failed");
    } else {
        // Parent process
        int status;
        do {
            waitpid(pid, &status, WUNTRACED);
        } while (!WIFEXITED(status) && !WIFSIGNALED(status));
    }

    return 1;
}