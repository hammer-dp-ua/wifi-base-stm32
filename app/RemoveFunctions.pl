use strict;
use Getopt::Long;
use PadWalker qw(peek_my peek_our peek_sub closed_over);
use List::Util qw(first);

my $userCreatedFilesParam = "main.c";

GetOptions("files=s" => \$userCreatedFilesParam);

sub main()
{
   my @userCreatedFiles = split(',', $userCreatedFilesParam);
   my %userInvokedFunctions;
   
   foreach my $userCreatedFile (@userCreatedFiles)
   {
      die $userCreatedFile . " file doesn't exist\n" unless -e $userCreatedFile; # -e  File exists
      
      addUserInvokedFunctions(\%userInvokedFunctions, $userCreatedFile);
   }
   
   opendir(DIR, ".") or die "Can't open directory";
   while (my $file = readdir(DIR))
   {
      if ($file =~ /.+?\.c$/ && !(first {$_ eq $file} @userCreatedFiles))
      {
         commentLibraryFunctions($file, \%userInvokedFunctions);
      }
   }
}

sub addUserInvokedFunctions
{
   my ($userInvokedFunctions, $userCreatedFileName) = @_;
   
   open(my $fh, '<', $userCreatedFileName) or die "Can't open $userCreatedFileName file";
   my @userCreatedFileLines = <$fh>;
   
   for (my $index = 0; $index < scalar @userCreatedFileLines; $index++)
   {
      my $row = $userCreatedFileLines[$index];
      
      if (!isRowCommented(\@userCreatedFileLines, $index) && isCalledFunctionPresents($row))
      {
         my $openBraceIndex = getOpenBraceIndex($row, 0);
         
         while ($openBraceIndex)
         {
            my $functionName = getFunctionName($row, $openBraceIndex);
            
            if (isFunctionName($functionName))
            {
               $userInvokedFunctions->{$functionName} = FunctionInfo->new($userCreatedFileName, $index);
               #print $functionName . "\n";
            }
            
            $openBraceIndex = getOpenBraceIndex($row, $openBraceIndex + 1);
         }
      }
   }
   close($fh);
}

sub isCalledFunctionPresents
{
   my $row = shift;
   my $isFunction;
   
   if ($row =~ /\w+\(.*?\)/ && $row !~ /^\w+\s+.+?\)$/) # Not a function yet. Ignore function definition
   {
      my $openBraceIndex = getOpenBraceIndex($row, 0);
      
      while ($openBraceIndex)
      {
         my $functionName = getFunctionName($row, $openBraceIndex);
         
         if (isFunctionName($functionName))
         {
            $isFunction = 1;
            last;
         }
         
         $openBraceIndex = getOpenBraceIndex($row, $openBraceIndex + 1);
      }
   }
   
   return $isFunction
}

sub isFunctionName
{
   my $functionName = shift;
   
   if ($functionName && $functionName ne "if" && $functionName ne "while" && $functionName ne "for" && $functionName ne "switch")
   {
      return 1;
   }
   else
   {
      return 0;
   }
}

sub getOpenBraceIndex
{
   my ($row, $startSearchingAt) = @_;
   my $openBraceIndex = 0;
   
   for (my $i = $startSearchingAt; $i < length($row); $i++)
   {
      if (substr($row, $i, 1) eq "(")
      {
         $openBraceIndex = $i;
         last;
      }
   }
   return $openBraceIndex;
}

sub getFunctionName
{
   my ($row, $openBraceIndex) = @_;
   my $functionNameFirstCharacterIndex;
   
   for (my $i = $openBraceIndex - 1; $i > 0; $i--)
   {
      if (substr($row, $i, 1) =~ /\W/)
      {
         $functionNameFirstCharacterIndex = $i + 1;
         last;
      }
   }
   
   my $functionNameLength = $openBraceIndex - $functionNameFirstCharacterIndex;
   return substr($row, $functionNameFirstCharacterIndex, $functionNameLength);
}

sub isRowCommented
{
   my @userCreatedFileLines = @{$_[0]};
   my $lineNumber = $_[1];
   my $isCommented;
   my $row = $userCreatedFileLines[$lineNumber];
   
   if ($row =~ /^\s*\/\/./ || $row =~ /^\s*\/\*./)
   {
      $isCommented = 1;
   }
   else
   {
      if ($row =~ /.+?\*\/$/ && $row !~ /\/\*/)
      {
         $isCommented = 1;
      }
      else
      {
         my $commentBegins;
         for (my $index = 0; $index < $lineNumber; $index++)
         {
            $row = $userCreatedFileLines[$index];
            
            if ($commentBegins && $row =~ /\*\//)
            {
               $commentBegins = 0;
            }
            
            if ($row =~ /\/\*/ && $row !~ /\*\//)
            {
               $commentBegins = 1;
            }
         }
         
         if ($commentBegins)
         {
            $isCommented = 1;
         }
      }
   }
   return $isCommented;
}

sub commentLibraryFunctions
{
   my $libraryFile = shift;
   my %userInvokedFunctions = %{shift()};
   my $commentThisFunction;
   my $fileChanged;
   
   open(my $fh, '<', $libraryFile) or die "Can't open $libraryFile file";
   my @libraryFileLines = <$fh>;
   close $fh;
   
   for (my $i = 0; $i < scalar @libraryFileLines; $i++)
   {
      my $line = $libraryFileLines[$i];
      
      if (!$commentThisFunction && $line =~ /^\w+\s(\w+)\(.*?\)$/)
      {
         my $libraryFunctionName = $1;
         
         if (!$userInvokedFunctions{$libraryFunctionName})
         {
            # This library function doesn't contain in the list of user invoked functions
            print $libraryFunctionName . " library function commented.\n";
            $libraryFileLines[$i] = "/*" . $line;
            $commentThisFunction = 1;
            $fileChanged = 1;
         }
      }
      elsif ($commentThisFunction && $line =~ /^\}$/)
      {
         $commentThisFunction = 0;
         $libraryFileLines[$i] = "}*/\n";
      }
      elsif ($commentThisFunction && $line =~ /\/\*/)
      {
         # If some multiline comment is present
         $line =~ s/\/\*/\/\//; # Replace "/*" with "//"
         $line =~ s/\*\///; # Remove "*/"
         $libraryFileLines[$i] = $line;
      }
   }
   
   if ($fileChanged)
   {
      open(my $fh, '>', $libraryFile);
      print $fh @libraryFileLines;
      close $fh;
   }
}

main();

package FunctionInfo;

sub new
{
   my $class = shift;
   my ($fileName, $lineNumber) = @_;
   
   my $self = bless
   {
      fileName => $fileName,
      lineNumber => $lineNumber,
   }, $class;
   return $self;
}