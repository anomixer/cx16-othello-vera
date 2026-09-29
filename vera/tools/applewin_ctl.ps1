<#
applewin_ctl.ps1 - drive the running AppleWin instance: inject keys, take a
screenshot, report the window handle.

AppleWin's main window is a modeless dialog (class #32770) with an empty
caption, so it is located by process id rather than by title.
#>
param(
    [int]$Pid2 = 0,
    [string[]]$Commands = @(),
    [int[]]$Keys = @(),
    [int]$KeyDelayMs = 150,
    [string]$Capture = '',
    [string]$Grab = '',
    [switch]$Screenshot,
    [switch]$List
)
$ErrorActionPreference = 'Stop'
Add-Type -ReferencedAssemblies System.Drawing @'
using System;using System.Text;using System.Drawing;using System.Drawing.Imaging;using System.Runtime.InteropServices;
public class AWin {
 public delegate bool CB(IntPtr h,IntPtr p);
 [DllImport("user32.dll")]static extern bool EnumWindows(CB c,IntPtr p);
 [DllImport("user32.dll")]static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern int GetClassName(IntPtr h,StringBuilder s,int n);
 [DllImport("user32.dll")]public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll")]static extern bool IsWindowVisible(IntPtr h);
 [StructLayout(LayoutKind.Sequential)]public struct Rect{public int L,T,R,B;}
 [DllImport("user32.dll")]static extern bool GetWindowRect(IntPtr h,out Rect r);
 [DllImport("user32.dll")]static extern bool PrintWindow(IntPtr h,IntPtr dc,uint flags);
 [DllImport("user32.dll")]public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")]public static extern bool ShowWindow(IntPtr h,int n);
 [DllImport("gdi32.dll")]public static extern IntPtr CreateCompatibleDC(IntPtr dc);
 [DllImport("gdi32.dll")]public static extern IntPtr CreateCompatibleBitmap(IntPtr dc,int w,int h);
 [DllImport("gdi32.dll")]public static extern IntPtr SelectObject(IntPtr dc,IntPtr obj);
 [DllImport("gdi32.dll")]public static extern bool BitBlt(IntPtr dst,int dx,int dy,int w,int h,IntPtr src,int sx,int sy,uint rop);
 [DllImport("gdi32.dll")]public static extern bool DeleteObject(IntPtr obj);
 [DllImport("gdi32.dll")]public static extern bool DeleteDC(IntPtr dc);
 [DllImport("user32.dll")]public static extern IntPtr GetDC(IntPtr h);
 [DllImport("user32.dll")]public static extern int ReleaseDC(IntPtr h,IntPtr dc);
 public static void Grab(IntPtr h,string file){
  ShowWindow(h,1); SetForegroundWindow(h);
  System.Threading.Thread.Sleep(300);
  Rect r;GetWindowRect(h,out r);
  int w=Math.Max(1,r.R-r.L), ht=Math.Max(1,r.B-r.T);
  Console.WriteLine("rect "+r.L+","+r.T+" "+w+"x"+ht);
  IntPtr screen=GetDC(IntPtr.Zero);
  IntPtr mem=CreateCompatibleDC(screen);
  IntPtr bmp=CreateCompatibleBitmap(screen,w,ht);
  SelectObject(mem,bmp);
  BitBlt(mem,0,0,w,ht,screen,r.L,r.T,0x00CC0020);
  using(var img=Image.FromHbitmap(bmp)){ img.Save(file,ImageFormat.Png); }
  DeleteObject(bmp); DeleteDC(mem); ReleaseDC(IntPtr.Zero,screen);
 }
 public static IntPtr Find(uint pid){
  IntPtr found=IntPtr.Zero;
  EnumWindows((h,p)=>{uint id;GetWindowThreadProcessId(h,out id);
   if(id==pid && IsWindowVisible(h)){var c=new StringBuilder(256);GetClassName(h,c,256);
    if(c.ToString()=="#32770")found=h;}
   return true;},IntPtr.Zero);
  return found;
 }
 public static void Save(IntPtr h,string file){
  Rect r;GetWindowRect(h,out r);
  using(var b=new Bitmap(Math.Max(1,r.R-r.L),Math.Max(1,r.B-r.T))){
   using(var g=Graphics.FromImage(b)){var dc=g.GetHdc();PrintWindow(h,dc,1);g.ReleaseHdc(dc);}
   b.Save(file);
  }
 }
}
'@

if ($Pid2 -eq 0) {
    $proc = Get-Process AppleWin -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $proc) { throw 'AppleWin is not running' }
    $Pid2 = $proc.Id
}
$h = [AWin]::Find([uint32]$Pid2)
if ($h -eq [IntPtr]::Zero) { throw "AppleWin main window not found (pid $Pid2)" }

function Send-VirtualKey([int]$key) {
    [void][AWin]::PostMessage($h, 0x100, [IntPtr]$key, [IntPtr]1)
    [void][AWin]::PostMessage($h, 0x101, [IntPtr]$key, [IntPtr]0xC0000001)
}

foreach ($command in $Commands) {
    foreach ($ch in $command.ToCharArray()) {
        [void][AWin]::PostMessage($h, 0x102, [IntPtr][int]$ch, [IntPtr]0)
    }
    Send-VirtualKey 13
    Start-Sleep -Milliseconds 200
}
foreach ($key in $Keys) {
    [void][AWin]::PostMessage($h, 0x102, [IntPtr]$key, [IntPtr]0)
    Start-Sleep -Milliseconds $KeyDelayMs
}
if ($Capture) {
    [AWin]::Save($h, $Capture)
    Write-Output "captured $Capture"
}
if ($Grab) {
    [AWin]::Grab($h, $Grab)
    Write-Output "grabbed $Grab"
}
if ($Screenshot) {
    # AppleWin hotkey id VK_SNAPSHOT_560 = WM_USER+4; writes a BMP into the
    # emulator's current folder named <diskimage>_NNNNNNNNN.bmp
    [void][AWin]::PostMessage($h, 0x0311, [IntPtr]0x0404, [IntPtr]0)
    Start-Sleep -Milliseconds 400
    Write-Output "screenshot requested (WM_HOTKEY VK_SNAPSHOT_560)"
}
Write-Output "AppleWin PID=$Pid2 HWND=$h"
