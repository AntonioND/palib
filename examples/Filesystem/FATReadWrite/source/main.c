/* FAT functions....
 * Read / Write
*/

#include <PA9.h>       // Include for PA_Lib
#include <fat.h>

int main()
{
	PA_Init();    // Initializes PA_Lib

	PA_LoadDefaultText(1, 0);  // Initialise the text system on the top screen

	PA_WaitForVBL();  PA_WaitForVBL();  PA_WaitForVBL();  // wait a few VBLs

	if (!fatInitDefault()) //Initialise fat library
	{
		PA_OutputSimpleText(1, 1, 1, "Can't initialize FAT");
		while (1)
			PA_WaitForVBL();
	}

	PA_OutputSimpleText(1, 1, 1, "FAT Test - Do not power off");
	PA_OutputSimpleText(1, 1, 3, "Opening File for Write");

	FILE* testWrite = fopen ("FATTest.txt", "wb"); //wb = create/truncate & write 
	if (testWrite == NULL)
	{
		PA_OutputSimpleText(1, 1, 4, "Failed to open File for Write");
		while (1)
			PA_WaitForVBL();
	}

	fwrite("Just writing text to a file :)", 30, 1, testWrite);
	fclose(testWrite);

	PA_OutputSimpleText(1, 1, 4, "Writing complete.");

	PA_OutputSimpleText(1, 1, 6, "Opening File for Read");
	FILE* testRead = fopen ("FATTest.txt", "rb"); //rb = read
	if (testRead == NULL)
	{
		PA_OutputSimpleText(1, 1, 7, "Failed to open File for Read");
		while (1)
			PA_WaitForVBL();
	}

	char filetext[30];
	fread(filetext, 30, 1, testRead);
	fclose(testRead);

	PA_OutputSimpleText(1, 1, 7, "FATTest.txt contains:");
	PA_OutputText(1, 2, 8, "%s", filetext);
	PA_OutputSimpleText(1, 1, 10, "Read Complete.");

	PA_OutputSimpleText(1, 1, 13, "Safe to power off.");

	while (1)
		PA_WaitForVBL();

	return 0;
}
