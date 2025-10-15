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
#include <sys/stat.h>

#define MAX_FILES 20

// Globale Optionen in einer Struktur speichern (statt globale bools)
typedef struct {
    bool recursive;
    bool case_insensitive;
} SearchOptions;

// Struktur für Optionen initialisieren
SearchOptions g_opts = { false, false };

// Funktion zur Prüfung, ob Dateiname mit Suchkriterium übereinstimmt
bool filename_matches(const char *filename, const char *fileToSearch, const SearchOptions *opts) {
    if (opts->case_insensitive) {
        return strcasecmp(filename, fileToSearch) == 0;
    }
    return strcmp(filename, fileToSearch) == 0; //ein byte- bzw. zeichen-genauer Vergleich unter Berücksichtigung der Groß-/Kleinschreibung
}

// Hilfsfunktion: Prüfen ob Pfad ein Verzeichnis ist
bool is_directory(const char *path) {
    struct stat st;
    if (lstat(path, &st) == -1) return false;
    return S_ISDIR(st.st_mode);
}

// Hilfsfunktion: Prüfen ob Pfad eine reguläre Datei ist
bool is_regular_file(const char *path) {
    struct stat st;
    if (lstat(path, &st) == -1) return false;
    return S_ISREG(st.st_mode);
}

// Treffer-Ausgabe (PID, gesuchter Dateiname, absoluter Pfad)
void report_match(const char *filename, const char *abs_path) {
    flockfile(stdout); //Synchonisierung: Sperrt stdout für andere Prozesse
    printf("%d: %s: %s\n", getpid(), filename, abs_path);
    funlockfile(stdout); //Gibt stdout wieder frei
}

// Funktion zum Durchsuchen eines Verzeichnisses
// Durchsucht ein Verzeichnis (rekursiv) nach einer Datei mit dem Namen "filename"
void search_in_directory(const char *dir_path, const char *filename, const SearchOptions *opts) {
    DIR *dp = opendir(dir_path);
    if (!dp) {
        fprintf(stderr, "Error opening directory '%s': %s\n", dir_path, strerror(errno));
        return;
    }

    struct dirent *entry;
    errno = 0;
    while ((entry = readdir(dp)) != NULL) {
        char path[PATH_MAX];
        if (snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name) >= (int)sizeof(path)) {
            fprintf(stderr, "Warning: Path too long, skipping: %s/%s\n", dir_path, entry->d_name);
            continue;
        }

        // "." und ".." überspringen
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        if (is_regular_file(path)) {
            // Reguläre Datei prüfen
            if (filename_matches(entry->d_name, filename, opts)) {
                char abs_path[PATH_MAX];
                if (realpath(path, abs_path)) { //speichert den absoluten Pfad von path in abs_path
                    report_match(filename, abs_path);
                } else {
                    fprintf(stderr, "PID %d: Error with realpath(%s): %s\n", getpid(), path, strerror(errno));
                }
            }
        } else if (is_directory(path) && opts->recursive) {
            // Rekursiv in Unterverzeichnisse gehen
            search_in_directory(path, filename, opts);
        }

        errno = 0;
    }

    if (errno != 0) {
        fprintf(stderr, "PID %d: Fehler bei readdir(%s): %s\n", getpid(), dir_path, strerror(errno));
    }

    closedir(dp);
}

// Parsing der Kommandozeilenargumente
void parse_arguments(int argc, char *argv[], char **search_path, char **filenames, int *file_count, SearchOptions *opts) {
    int opt;
    while ((opt = getopt(argc, argv, "Ri")) != -1) { //getopt gibt -1 zurück, wenn keine Optionen mehr vorhanden sind
        switch (opt) {
            case 'R':
                opts->recursive = true;
                break;
            case 'i':
                opts->case_insensitive = true;
                break;
            default:  //Wenn eine unbekannte Option gefunden wurde
                fprintf(stderr, "Usage: %s [-R] [-i] searchpath filename1 [filename2] ... [filenameN]\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    // Remaining arguments
    if (optind < argc) {   //optind: Globale Variable von getopt, zeigt auf das nächste zu verarbeitende Argument
        *search_path = argv[optind++]; //Post-Inkrement - verwendet den aktuellen Wert und erhöht dann optind
    } else {
        fprintf(stderr, "Search path is required.\n");
        exit(EXIT_FAILURE);
    }

    while (optind < argc) {
        if (*file_count < MAX_FILES) {
            filenames[(*file_count)++] = argv[optind++];
        } else {
            fprintf(stderr, "Too many filenames provided.\n");
            exit(EXIT_FAILURE);
        }
    }
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

    parse_arguments(argc, argv, &search_path, filenames, &file_count, &g_opts);

    // Fork a child process for each filename
    for (int i = 0; i < file_count; i++) {
        pid_t pid = fork();
        if (pid == 0) { // Child process
            search_in_directory(search_path, filenames[i], &g_opts);
            exit(EXIT_SUCCESS); // Exit child process
        } else if (pid < 0) {
            fprintf(stderr, "Error: fork() failed for '%s': %s\n", filenames[i], strerror(errno));
            while (wait(NULL) > 0); // Aufräumen
            exit(EXIT_FAILURE);
        }
    }

    // Parent process waits for all child processes
    while (wait(NULL) > 0);

    return 0;
}
