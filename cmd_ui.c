/*
 * Copyright (C) 2017 Niko Rosvall <niko@byteptr.com>
 */

#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include "cmd_ui.h"
#include "entry.h"
#include "db.h"
#include "utils.h"
#include "crypto.h"

extern int fileno(FILE *stream);

/*Removes new line character from a string.*/
static void strip_newline_str(char *str)
{

    char *i = str;
    char *j = str;

    while (*j != '\0')
    {
        *i = *j++;

        if(*i != '\n')
            i++;
    }

    *i = '\0';
}

/*Turns echo of from the terminal and asks for a passphrase.
 *Usually stream is stdin. Returns length of the passphrase,
 *passphrase is stored to lineptr. Lineptr must be allocated beforehand.
 */
static size_t my_getpass(char *prompt, char **lineptr, size_t *n, FILE *stream)
{
    struct termios old, new;
    int nread;

    /*Turn terminal echoing off.*/
    if(tcgetattr(fileno(stream), &old) != 0)
        return -1;

    new = old;
    new.c_lflag &= ~ECHO;

    if(tcsetattr(fileno(stream), TCSAFLUSH, &new) != 0)
        return -1;

    if(prompt)
        printf("%s", prompt);

    /*Read the password.*/
    nread = getline(lineptr, n, stream);

    if(nread >= 1 && (*lineptr)[nread - 1] == '\n')
    {
        (*lineptr)[nread - 1] = 0;
        nread--;
    }

    printf("\n");

    /*Restore terminal echo.*/
    tcsetattr(fileno(stream), TCSAFLUSH, &old);

    return nread;
}

void init_database(const char *path, int force)
{
    if(!has_active_database() || force == 1)
    {
        //If forced, delete any existing file
        if(force == 1)
        {
            if(file_exists(path))
                unlink(path);
        }
            
        if(db_init_new(path))
            write_active_database_path(path);
    }
    else
    {
        fprintf(stderr, "Existing database is already active. "
                "Encrypt it before creating a new one.\n");
    }
}

void decrypt_database(const char *path)
{
    if(has_active_database())
    {
        fprintf(stderr, "Existing database is already active. "
                "Encrypt it before decrypting another one.\n");
                
        return;
    }
    
    size_t pwdlen = 1024;
    char pass[pwdlen];
    char *ptr = pass;
    
    my_getpass("Password: ", &ptr, &pwdlen, stdin);
    
    if(!decrypt_file(pass, path))
    {
        fprintf(stderr, "Failed to decrypt %s.\n", path);
        return;
    }
    
    write_active_database_path(path);
}

void encrypt_database()
{
    if(!has_active_database())
    {
        fprintf(stderr, "No decrypted database found.\n");
        return;
    }
    
    size_t pwdlen = 1024;
    char pass[pwdlen];
    char *ptr = pass;
    char *path = NULL;
    char *lockfile_path = NULL;
    
    path = read_active_database_path();
    
    if(!path)
    {
        fprintf(stderr, "Unable to read activate database path.\n");
        return;
    }
    
    my_getpass("Password: ", &ptr, &pwdlen, stdin);
    
    //TODO: ask the pass twice to make sure user typed it correctly
    
    if(!encrypt_file(pass, path))
    {
        fprintf(stderr, "Encryption of %s failed.\n", path);
        free(path);
        return;
    }
    
    free(path);
    
    lockfile_path = get_lockfile_path();
    
    if(!lockfile_path)
    {
        fprintf(stderr, "Unable to retrieve the lock file path.\n");
        return;
    }
    
    //Finally delete the file that holds the activate database path.
    //This way we allow Titan to create a new database or open another one.
    unlink(lockfile_path);
    free(lockfile_path);
}

/* Interactively adds a new entry to the database */
bool add_new_entry()
{
    if(!has_active_database())
    {
        fprintf(stderr, "No decrypted database found.\n");
        return false;
    }

    char title[1024] = {0};
    char user[1024] = {0};
    char url[1024] = {0};
    char notes[1024] = {0};
    size_t pwdlen = 1024;
    char pass[pwdlen];
    char *ptr = pass;

    fprintf(stdout, "Title: ");
    fgets(title, 1024, stdin);
    fprintf(stdout, "Username: ");
    fgets(user, 1024, stdin);
    fprintf(stdout, "Url: ");
    fgets(url, 1024, stdin);
    fprintf(stdout, "Notes: ");
    fgets(notes, 1024, stdin);

    my_getpass("Password: ", &ptr, &pwdlen, stdin);

    strip_newline_str(title);
    strip_newline_str(user);
    strip_newline_str(url);
    strip_newline_str(notes);

    Entry_t *entry = entry_new(title, user, url, pass,
                               notes);

    if(!entry)
        return false;

    if(!db_insert_entry(entry))
    {
        fprintf(stderr, "Failed to add a new entry.\n");
        return false;
    }

    entry_free(entry);

    return true;
}

bool edit_entry(int id)
{
    if(!has_active_database())
    {
        fprintf(stderr, "No decrypted database found.\n");
        return false;
    }

    Entry_t *entry = db_get_entry_by_id(id);

    if(!entry)
        return false;

    if(entry->id == -1)
    {
        printf("Nothing found.\n");
        free(entry);
        return false;
    }

    char title[1024] = {0};
    char user[1024] = {0};
    char url[1024] = {0};
    char notes[1024] = {0};
    size_t pwdlen = 1024;
    char pass[pwdlen];
    char *ptr = pass;
    bool update = false;

    fprintf(stdout, "Current title %s\n", entry->title);
    fprintf(stdout, "New title: ");
    fgets(title, 1024, stdin);
    fprintf(stdout, "Current username %s\n", entry->user);
    fprintf(stdout, "New username: ");
    fgets(user, 1024, stdin);
    fprintf(stdout, "Current url %s\n", entry->url);
    fprintf(stdout, "New url: ");
    fgets(url, 1024, stdin);
    fprintf(stdout, "Current notes %s\n", entry->notes);
    fprintf(stdout, "New note: ");
    fgets(notes, 1024, stdin);
    fprintf(stdout, "Current password %s\n", entry->password);
    my_getpass("New password: ", &ptr, &pwdlen, stdin);

    strip_newline_str(title);
    strip_newline_str(user);
    strip_newline_str(url);
    strip_newline_str(notes);

    if(title[0] != '\0')
    {
        entry->title = strdup(title);
        update = true;
    }
    if(user[0] != '\0')
    {
        entry->user = strdup(user);
        update = true;
    }
    if(url[0] != '\0')
    {
        entry->url = strdup(url);
        update = true;
    }
    if(notes[0] != '\0')
    {
        entry->notes = strdup(notes);
        update = true;
    }
    if(pass[0] != '\0')
    {
        entry->password = strdup(pass);
        update = true;
    }

    if(update)
        db_update_entry(entry->id, entry);

    entry_free(entry);

    return true;
}

bool remove_entry(int id)
{
    if(!has_active_database())
    {
        fprintf(stderr, "No decrypted database found.\n");
        return false;
    }

    bool changes = false;

    if(db_delete_entry(id, &changes))
    {
        if(changes == true)
            fprintf(stdout, "Entry was deleted from the database.\n");
        else
            fprintf(stdout, "No entry with id %d was found.\n", id);

        return true;
    }

    return false;
}

void list_by_id(int id, int show_password)
{
    if(!has_active_database())
    {
        fprintf(stderr, "No decrypted database found.\n");
        return;
    }

    Entry_t *entry = db_get_entry_by_id(id);

    if(!entry)
        return;


    if(entry->id == -1)
    {
        printf("Nothing found with id %d.\n", id);
        free(entry);
        return;
    }

    fprintf(stdout, "=====================================================================\n");
    fprintf(stdout, "Title: %s\n", entry->title);
    fprintf(stdout, "User:  %s\n", entry->user);
    fprintf(stdout, "Url:   %s\n", entry->url);
    fprintf(stdout, "Notes: %s\n", entry->notes);
    fprintf(stdout, "Modified: %s\n", entry->stamp);

    if(show_password == 1)
        fprintf(stdout, "Password: %s\n", entry->password);
    else
        fprintf(stdout, "Password: **********\n");

    fprintf(stdout, "=====================================================================\n");
    entry_free(entry);
}

/* Loop through all entries in the database.
 * At the moment printing to stdout is handled by sqlite callback.
 * While it's ok for the command line, but if we ever need GUI
 * then this needs to be designed better using a linked list etc.
 */
void list_all(int show_password)
{
    if(!has_active_database())
    {
        fprintf(stderr, "No decrypted database found.\n");
        return;
    }

    db_list_all(show_password);
}

/* Uses sqlite "like" query and prints results to stdout.
 * This is ok for the command line version of Titan. However
 * better design is needed _if_ GUI version will be developed.
 */
void find(const char *search, int show_password)
{
    if(!has_active_database())
    {
        fprintf(stderr, "No decrypted database found.\n");
        return;
    }

    db_find(search, show_password);
}

void show_current_db_path()
{
    char *path = NULL;

    path = read_active_database_path();

    if(!path)
    {
        fprintf(stderr, "No decrypted database exist.\n");
    }
    else
    {
        fprintf(stdout, "%s\n", path);
        free(path);
    }
}

void set_use_db(const char *path)
{
    if(has_active_database())
    {
        fprintf(stderr, "Current database is decrypted, encrypt it first.\n");
        return;
    }

    write_active_database_path(path);
}
