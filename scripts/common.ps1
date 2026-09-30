# Load the modules belonging to this shell. A launcher started from PowerShell
# 7 can otherwise inherit module paths that Windows PowerShell 5 cannot load.
foreach ($blvrModule in @('Microsoft.PowerShell.Management','Microsoft.PowerShell.Utility')) {
    Import-Module ($PSHOME+'\Modules\'+$blvrModule+'\'+$blvrModule+'.psd1') -ErrorAction Stop
}
