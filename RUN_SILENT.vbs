Set fso = CreateObject("Scripting.FileSystemObject")
Set f = fso.GetFile(WScript.ScriptFullName)
folder = f.ParentFolder
CreateObject("WScript.Shell").Run "cmd /c """ & folder & "\RUN_ME.bat""", 0, False
