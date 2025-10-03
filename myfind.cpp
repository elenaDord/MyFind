#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdbool.h>
#include <limits.h>
#include <ctype.h>
#include <errno.h>
#include <string.h>

#define MAX_FILES 20

bool g_recursive = false;
bool g_case_insensitive = false;

// Function to check if a filename matches the search criteria
bool matches(const char *filename, const char *fileToSearch) {
    if (g_case_insensitive) {
        return strcasecmp(filename, fileToSearch) == 0;
    }
    return strcmp(filename, fileToSearch) == 0; //ein byte- bzw. zeichen-genauer Vergleich unter Berücksichtigung der Groß-/Kleinschreibung
}

// Function to search for files in a directory
// Durchsucht ein Verzeichnis (rekursiv) nach einer Datei mit dem Namen "filename"
void search_directory(const char *dir_path, const char *filename) {
    DIR *dp = opendir(dir_path);
    if (!dp) {
        perror("opendir");
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dp)) != NULL) {

        // in path wird der Pfad vom eigegebenen Pfad bis zur gesuchten Datei gespeichert
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name);

        if (entry->d_type == DT_REG) {  //DT_REG: Konstante für "regular file"
            // Reguläre Datei prüfen
            if (matches(entry->d_name, filename)) {
                char abs_path[PATH_MAX];
                if (realpath(path, abs_path)) { //speichert den absoluten Pfad von path in abs_path
                    flockfile(stdout); //Synchonisierung: Sperrt stdout für andere Prozesse
                    printf("%d: %s: %s\n", getpid(), filename, abs_path);
                    funlockfile(stdout); //Gibt stdout wieder frei
                }
            }
        } else if (entry->d_type == DT_DIR) {
        // "." und ".." überspringen
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            if (g_recursive) {
                search_directory(path, filename);
            }
        }
    }

    closedir(dp);
}

/*
 * Parallelisierung:
 * Für jede gesuchte Datei wird mit fork() ein eigener Kindprozess gestartet.
 * Jeder Kindprozess durchsucht (rekursiv oder nicht) das angegebene Suchverzeichnis.
 * Wird ein Treffer gefunden, wird dieser vom Kindprozess mit seiner PID auf stdout ausgegeben.
 * Der Elternprozess wartet anschließend mit wait() auf alle Kindprozesse und verhindert so Zombies.
 */

int main(int argc, char *argv[]) {
    char *search_path = NULL;
    char *filenames[MAX_FILES];
    int file_count = 0;

    // Parse command line arguments
    int opt;
    while ((opt = getopt(argc, argv, "Ri")) != -1) { //getopt gibt -1 zurück, wenn keine Optionen mehr vorhanden sind
        switch (opt) {
            case 'R':
                g_recursive = true;
                break;
            case 'i':
                g_case_insensitive = true;
                break;
            default:  //Wenn eine unbekannte Option gefunden wurde
                fprintf(stderr, "Usage: %s [-R] [-i] searchpath filename1 [filename2] ... [filenameN]\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    // Remaining arguments
    if (optind < argc) {   //optind: Globale Variable von getopt, zeigt auf das nächste zu verarbeitende Argument
        search_path = argv[optind++]; //Post-Inkrement - verwendet den aktuellen Wert und erhöht dann optind
    } else {
        fprintf(stderr, "Search path is required.\n");
        exit(EXIT_FAILURE);
    }

    while (optind < argc) {
        if (file_count < MAX_FILES) {
            filenames[file_count++] = argv[optind++];
        } else {
            fprintf(stderr, "Too many filenames provided.\n");
            exit(EXIT_FAILURE);
        }
    }

    // Fork a child process for each filename
    for (int i = 0; i < file_count; i++) {
        pid_t pid = fork();
        if (pid == 0) { // Child process
                search_directory(search_path, filenames[i]);
            exit(EXIT_SUCCESS); // Exit child process
        } else if (pid < 0) {
            perror("fork");
            exit(EXIT_FAILURE);
        }
    }

    // Parent process waits for all child processes
    while (wait(NULL) > 0);

    return 0;
}
