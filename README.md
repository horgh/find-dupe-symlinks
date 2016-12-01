This program recursively descends into the given directory and looks for all
symbolic links. It checks whether there are any duplicate link targets.

It does not follow any symbolic links.

For example, if you have a directory with the following structure:

    /home/test
      /home/test/symlink1 -> /home/test2
      /home/test/symlink2 -> /home/test2
      /home/test/symlink3 -> /home/test3
      /home/test/regularfile

This program will report that /home/test/symlink1 and /home/test/symlink2 are
duplicates.
