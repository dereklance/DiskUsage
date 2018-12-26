/*
   Imitates the functionality of the linux command `du`.
   Valid options are --max-depth= and -h
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <dirent.h>
#include <limits.h>

#define MAX_FILES 128
#define FILE_LEN 128
#define OUT_FORMAT "%-8ld%s\n"
#define RD_FORMAT "%-8s%s\n"
#define BYTE_CONV 1024

typedef struct {
   int all;
   int total;
   int readable;
   long depth;
   char *progName;
} Mode;

int isFile(mode_t type) {
   return S_ISREG(type) || S_ISLNK(type);
}

int isDir(mode_t type) {
   return S_ISDIR(type);
}

int ceiling(double x) {
   return x - (int)x > 0 ? (int)(x + 1) : (int)x;
}

void ReadCmdArguments(int argc, char **argv, Mode *mode, char **files) {
   int invalid = 0;

   for (argc--, argv++; argc; argc--, argv++) {
      if (strlen(*argv) >= 12 && **argv == '-' && argv[0][1] == '-') {
         char *str = "--max-depth=";
         int i;
         long depth;

         for (i = 0; i < 12; i++) {
            if (str[i] != argv[0][i])
               break;
         }
         errno = 0;
         if (i == 12) {
            depth = strtol(*argv + 12, NULL, 10);
            if (errno || depth < 0 || !argv[0][12]) {
               fprintf(stderr, "%s: invalid maximum depth `%s'\n",
                mode->progName, *argv + 12);
               invalid = 1;
            }
            mode->depth = depth;
         }
         else {
            fprintf(stderr, "%s: unrecognized option '%s'\n",
             mode->progName, *argv);
            invalid = 1;
         }
      }
      else if (**argv == '-' && argv[0][1] == '-') {
         fprintf(stderr, "%s: unrecognized option '%s'\n",
          mode->progName, *argv);
         invalid = 1;
      }
      else if (**argv == '-') {
         while (*(++(*argv))) {
            if (**argv == 'a')
               mode->all = 1;
            else if (**argv == 'c')
               mode->total = 1;
            else if (**argv == 'h')
               mode->readable = 1;
            else {
               fprintf(stderr, "%s: invalid option -- '%c'\n",
                mode->progName, **argv);
               invalid = 1;
            }
         }
      }
      else {
         *files++ = *argv;
      }
   }
   *files = (char *)NULL;
   if (invalid) {
      fprintf(stderr, "Try `du --help' for more information.\n");
      exit(EXIT_FAILURE);
   }
}

void PrintFileReadable(char *file, double kb) {
   double mb = kb / BYTE_CONV, gb = mb / BYTE_CONV, tb = gb / BYTE_CONV;
   char size[8];

   if (tb >= 1) {
      if (tb / 10 > 1)
         sprintf(size, "%dT", ceiling(tb));
      else
         sprintf(size, "%.1lfT", ceiling(tb * 10) / 10.0);
      printf(RD_FORMAT, size, file);
      return;
   }
   if (gb >= 1) {
      if (gb / 10 > 1)
         sprintf(size, "%dG", ceiling(gb));
      else
         sprintf(size, "%.1lfG", ceiling(gb * 10) / 10.0);
      printf(RD_FORMAT, size, file);
      return;
   }
   if (mb >= 1) {
      if (mb / 10 > 1)
         sprintf(size, "%dM", ceiling(mb));
      else
         sprintf(size, "%.1lfM", ceiling(mb * 10) / 10.0);
      printf(RD_FORMAT, size, file);
      return;
   }
   if (kb / 10 > 1)
      sprintf(size, "%dK", ceiling(kb));
   else if (!kb)
      sprintf(size, "0");
   else
      sprintf(size, "%.1lfK", ceiling(kb * 10) / 10.0);
   printf(RD_FORMAT, size, file);
}

long PrintFileUsage(long size, char *name, Mode *mode) {
   if (mode->readable)
      PrintFileReadable(name, (double)size);
   else
      printf(OUT_FORMAT, size, name);
   return size;
}

int DepthOK(long depth, Mode *mode) {
   return depth <= mode->depth || mode->depth == -1;
}

long PrintDirectoryUsage(char *file, Mode *mode, int depth) {
   DIR *dir = opendir(file);
   struct dirent *entry;
   struct stat buf;
   long total = 0;
   char cdir[PATH_MAX];

   lstat(file, &buf);
   total = buf.st_blocks / 2;

   if (!dir) {
      fprintf(stderr, "%s: cannot read directory `%s': ", mode->progName, file);
      perror(NULL);
      goto cleanup;
   }
   while((entry = readdir(dir))) {
      char *name = entry->d_name;

      if (!strcmp(name, ".") || !strcmp(name, ".."))
         continue;
      strcpy(cdir, file);
      if (strcmp(file, "/"))
         strcat(cdir, "/");
      strcat(cdir, name);

      lstat(cdir, &buf);
      if (isFile(buf.st_mode) && mode->all && DepthOK(depth + 1, mode))
         total += PrintFileUsage(buf.st_blocks / 2, cdir, mode);
      else if (isDir(buf.st_mode))
         total += PrintDirectoryUsage(cdir, mode, depth + 1);
      else
         total += buf.st_blocks / 2;
   }
   if (DepthOK(depth, mode))
      PrintFileUsage(total, file, mode);

   cleanup: closedir(dir);
   return total;
}

int PrintDiskUsage(char **files, Mode *mode) {
   struct stat buf;
   long total = 0;
   int retVal = 0;

   if (!*files)
      total = PrintDirectoryUsage(".", mode, 0);
   while (*files) {
      if (lstat(*files, &buf)) {
         fprintf(stderr, "%s: cannot access `%s': No such file or directory\n",
          mode->progName, *files);
         retVal = 1;
      }
      else if (isFile(buf.st_mode))
         total += PrintFileUsage(buf.st_blocks / 2, *files, mode);
      else if (isDir(buf.st_mode))
         total += PrintDirectoryUsage(*files, mode, 0);
      files++;
   }
   if (mode->total)
      PrintFileUsage(total, "total", mode);
   return retVal;
}

int main(int argc, char **argv) {
   Mode mode = {0, 0, 0, -1, *argv};
   char **files = malloc(MAX_FILES * sizeof(char *));
   int i;

   for (i = 0; i < MAX_FILES; i++)
      files[i] = malloc(FILE_LEN * sizeof(char));

   ReadCmdArguments(argc, argv, &mode, files);
   return PrintDiskUsage(files, &mode) ? EXIT_FAILURE : EXIT_SUCCESS;
}
