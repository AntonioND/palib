/* FAT functions....
*/

#include <PA9.h>       // Include for PA_Lib
#include <fat.h>
#include <dirent.h>
#include <sys/stat.h>

int main(int argc, char *argv[])
{
    PA_Init();

    PA_LoadDefaultText(0, 0); // Initialise the text system on the bottom screen
    PA_LoadDefaultText(1, 0); // Initialise the text system on the top screen

    if (!fatInitDefault()) // Initialise fat library
    {
        PA_OutputText(1, 1, 1, "Can't initialize FAT");
        while (1)
            PA_WaitForVBL();
    }

    DIR *dirp = opendir("/");
    if (dirp == NULL)
    {
        PA_OutputText(1, 1, 1, "Unable to open the directory.");
        while (1)
            PA_WaitForVBL();
    }

    int linenumber = 0;
    int screen = 1;

    while (1)
    {
        struct dirent *cur = readdir(dirp);
        if (cur == NULL)
            break;

        if (strlen(cur->d_name) == 0)
            break;

        // If we hit this twice we are overwriting on the bottom screen :(
        if (linenumber == 24)
        {
            screen = 0; // Output on bottom if we filled the top screen
            linenumber = 0; // Reset line number...
        }

        // "cur->d_type == DT_DIR" indicates a directory
        PA_OutputText(screen, 0, linenumber, "%02d%s: %s\n",
                      linenumber, (cur->d_type == DT_DIR) ? "D" : "-",
                      cur->d_name);

        linenumber++; // Next line
    }

    closedir(dirp);

    while (1)
        PA_WaitForVBL();

    return 0;
}
