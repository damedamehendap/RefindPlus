/*
 * libeg/screen.c
 * Screen handling functions
 *
 * Copyright (c) 2006 Christoph Pfisterer
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the
 *    distribution.
 *
 *  * Neither the name of Christoph Pfisterer nor the names of the
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * Modifications copyright (c) 2012-2020 Roderick W. Smith
 *
 * Modifications distributed under the terms of the GNU General Public
 * License (GPL) version 3 (GPLv3), or (at your option) any later version.
 *
 */
/*
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "libegint.h"
#include "../refind/screen.h"
#include "../refind/lib.h"
#include "../refind/mystrings.h"
#include "../include/refit_call_wrapper.h"
#include "libeg.h"
#include "../include/Handle.h"

#include <efiUgaDraw.h>
#include <efiConsoleControl.h>

#ifndef __MAKEWITH_GNUEFI
#define LibLocateProtocol EfiLibLocateProtocol
#endif

// Console defines and variables

static EFI_GUID ConsoleControlProtocolGuid = EFI_CONSOLE_CONTROL_PROTOCOL_GUID;
static EFI_CONSOLE_CONTROL_PROTOCOL *ConsoleControl = NULL;

static EFI_GUID UgaDrawProtocolGuid = EFI_UGA_DRAW_PROTOCOL_GUID;
static EFI_UGA_DRAW_PROTOCOL *UgaDraw = NULL;

static EFI_GUID GraphicsOutputProtocolGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
static EFI_GRAPHICS_OUTPUT_PROTOCOL *GraphicsOutput = NULL;

static BOOLEAN egHasGraphics = FALSE;
static UINTN egScreenWidth  = 800;
static UINTN egScreenHeight = 600;


// Added by Jief_Machak (https://sourceforge.net/u/jief7/profile) from Clover
VOID egDumpGOPVideoModes(VOID) {
    EFI_STATUS  Status;
    UINT32      MaxMode;
    UINT32      Mode;
    UINTN       SizeOfInfo;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
    CHAR16      *PixelFormatDesc;

    if (GraphicsOutput == NULL) {
        return;
    }

    // get dump
    MaxMode = GraphicsOutput->Mode->MaxMode;
    Mode = GraphicsOutput->Mode->Mode;
    Print(L"Available graphics modes for refit.conf screen_resolution:\n");
    Print(L"Curr. Mode = %d, Modes = %d, FB = %lx, FB size=0x%x\n",
           Mode, MaxMode, GraphicsOutput->Mode->FrameBufferBase, GraphicsOutput->Mode->FrameBufferSize);

    for (Mode = 0; Mode < MaxMode; Mode++) {
        Status = GraphicsOutput->QueryMode(GraphicsOutput, Mode, &SizeOfInfo, &Info);
        if (Status == EFI_SUCCESS) {

            switch (Info->PixelFormat) {
                case PixelRedGreenBlueReserved8BitPerColor:
                    PixelFormatDesc = L"8bit RGB";
                    break;

                case PixelBlueGreenRedReserved8BitPerColor:
                    PixelFormatDesc = L"8bit BGR";
                    break;

                case PixelBitMask:
                    PixelFormatDesc = L"BITMASK";
                    break;

                case PixelBltOnly:
                    PixelFormatDesc = L"NO FB";
                    break;

                default:
                    PixelFormatDesc = L"invalid";
                    break;
            }

            Print(L"- Mode %d: %dx%d PixFmt = %s, PixPerScanLine = %d\n",
                      Mode, Info->HorizontalResolution, Info->VerticalResolution, PixelFormatDesc, Info->PixelsPerScanLine);
        } else {
             Print(L"- Mode %d: %r\n", Mode, Status);
        }
    }

}

//
// Sets mode via GOP protocol, and reconnects simple text out drivers
//

static EFI_STATUS GopSetModeAndReconnectTextOut(IN UINT32 ModeNumber)
{
    EFI_STATUS  Status;

    if (GraphicsOutput == NULL) {
        return EFI_UNSUPPORTED;
    }

    Status = GraphicsOutput->SetMode(GraphicsOutput, ModeNumber);
    Print(L"Video mode change to mode #%d: %r\n", ModeNumber, Status);

    return Status;
}

EFI_STATUS egSetMode(INT32 Next)
{
  EFI_STATUS  Status = EFI_UNSUPPORTED;
  UINT32      MaxMode;
  UINTN       SizeOfInfo;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
  INT32      Mode;
  UINT32     Index = 0;

  if (GraphicsOutput == NULL) {
    return EFI_UNSUPPORTED;
  }

  MaxMode = GraphicsOutput->Mode->MaxMode;
  Mode = GraphicsOutput->Mode->Mode;
  while (EFI_ERROR(Status) && Index <= MaxMode) {
    Mode = Mode + Next;
    Mode = (Mode >= (INT32)MaxMode)?0:Mode;
    Mode = (Mode < 0)?((INT32)MaxMode - 1):Mode;
    Status = GraphicsOutput->QueryMode(GraphicsOutput, (UINT32)Mode, &SizeOfInfo, &Info);
    Print(L"QueryMode %d Status=%r\n", Mode, Status);
    if (Status == EFI_SUCCESS) {
      Status = GopSetModeAndReconnectTextOut((UINT32)Mode);
      egScreenWidth = GraphicsOutput->Mode->Info->HorizontalResolution;
      egScreenHeight = GraphicsOutput->Mode->Info->VerticalResolution;
    }
    Index++;
  }

  return Status;
}

EFI_STATUS egSetMaxResolution()
{
  EFI_STATUS  Status = EFI_UNSUPPORTED;
  UINT32      Width = 0;
  UINT32      Height = 0;
  UINT32      BestMode = 0;
  UINT32      MaxMode;
  UINT32      Mode;
  UINTN       SizeOfInfo;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;

  if (GraphicsOutput == NULL) {
    return EFI_UNSUPPORTED;
  }

  Print(L"SetMaxResolution: ");
  MaxMode = GraphicsOutput->Mode->MaxMode;
  for (Mode = 0; Mode < MaxMode; Mode++) {
    Status = GraphicsOutput->QueryMode(GraphicsOutput, Mode, &SizeOfInfo, &Info);
    if (Status == EFI_SUCCESS) {
      if (Width > Info->HorizontalResolution) {
        continue;
      }
      if (Height > Info->VerticalResolution) {
        continue;
      }
      Width = Info->HorizontalResolution;
      Height = Info->VerticalResolution;
      BestMode = Mode;
    }
  }
  Print(L"found best mode %d: %dx%d\n", BestMode, Width, Height);
  // check if requested mode is equal to current mode
  if (BestMode == GraphicsOutput->Mode->Mode) {
    Print(L" - already set\n");
    egScreenWidth = GraphicsOutput->Mode->Info->HorizontalResolution;
    egScreenHeight = GraphicsOutput->Mode->Info->VerticalResolution;
    Status = EFI_SUCCESS;
  } else {
    Status = GopSetModeAndReconnectTextOut(BestMode);
    if (Status == EFI_SUCCESS) {
      egScreenWidth = Width;
      egScreenHeight = Height;
      Print(L" - set\n", Status);
    } else {
      // we can not set BestMode - search for first one that we can
      Print(L" - %r\n", Status);
      Status = egSetMode(1);
    }
  }

  return Status;
}


//
// Screen handling
//

// Make the necessary system calls to identify the current graphics mode.
// Stores the results in the file-global variables egScreenWidth,
// egScreenHeight, and egHasGraphics. The first two of these will be
// unchanged if neither GraphicsOutput nor UgaDraw is a valid pointer.
static VOID egDetermineScreenSize(VOID) {
    EFI_STATUS Status = EFI_SUCCESS;
    UINT32 UGAWidth, UGAHeight, UGADepth, UGARefreshRate;

    // get screen size
    egHasGraphics = FALSE;
    if (GraphicsOutput != NULL) {
        egScreenWidth = GraphicsOutput->Mode->Info->HorizontalResolution;
        egScreenHeight = GraphicsOutput->Mode->Info->VerticalResolution;
        egHasGraphics = TRUE;
    } else if (UgaDraw != NULL) {
        Status = refit_call5_wrapper(UgaDraw->GetMode, UgaDraw, &UGAWidth, &UGAHeight, &UGADepth, &UGARefreshRate);
        if (EFI_ERROR(Status)) {
            UgaDraw = NULL;   // graphics not available
        } else {
            egScreenWidth  = UGAWidth;
            egScreenHeight = UGAHeight;
            egHasGraphics = TRUE;
        }
    }
} // static VOID egDetermineScreenSize()

VOID egGetScreenSize(OUT UINTN *ScreenWidth, OUT UINTN *ScreenHeight)
{
    egDetermineScreenSize();

    if (ScreenWidth != NULL)
        *ScreenWidth = egScreenWidth;
    if (ScreenHeight != NULL)
        *ScreenHeight = egScreenHeight;
}

VOID egInitScreen(VOID)
{
    EFI_STATUS Status = EFI_SUCCESS;

    // get protocols
    Status = LibLocateProtocol(&ConsoleControlProtocolGuid, (VOID **) &ConsoleControl);
    if (EFI_ERROR(Status)) {
        ConsoleControl = NULL;
        Print(L"ConsoleControl !ok\n");
    } else {
    	Print(L"ConsoleControl ok\n");
    }

    Status = LibLocateProtocol(&UgaDrawProtocolGuid, (VOID **) &UgaDraw);
    if (EFI_ERROR(Status)) {
        UgaDraw = NULL;
    	Print(L"UgaDraw !ok\n");
    } else {
    	Print(L"UgaDraw ok\n");
    }

    Status = LibLocateProtocol(&GraphicsOutputProtocolGuid, (VOID **) &GraphicsOutput);
    if (EFI_ERROR(Status)) {
        GraphicsOutput = NULL;
    	Print(L"GraphicsOutput !ok\n");
    } else {
    	Print(L"GraphicsOutput ok\n");
    }

//    egDetermineScreenSize();

    // get screen size
    egHasGraphics = FALSE;
    if (GraphicsOutput != NULL) {
        egDumpGOPVideoModes();
        egSetMaxResolution();
        egScreenWidth = GraphicsOutput->Mode->Info->HorizontalResolution;
        egScreenHeight = GraphicsOutput->Mode->Info->VerticalResolution;
        egHasGraphics = TRUE;
    } else if (UgaDraw != NULL) {
    	UINT32 Width, Height, Depth, RefreshRate;
        Status = UgaDraw->GetMode(UgaDraw, &Width, &Height, &Depth, &RefreshRate);
        if (EFI_ERROR(Status)) {
            UgaDraw = NULL;   // graphics not available
        } else {
            egScreenWidth  = Width;
            egScreenHeight = Height;
            egHasGraphics = TRUE;
        }
    }
}

// Convert a graphics mode (in *ModeWidth) to a width and height (returned in
// *ModeWidth and *Height, respectively).
// Returns TRUE if successful, FALSE if not (invalid mode, typically)
BOOLEAN egGetResFromMode(UINTN *ModeWidth, UINTN *Height) {
   UINTN                                 Size;
   EFI_STATUS                            Status;
   EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  *Info = NULL;

   if ((ModeWidth != NULL) && (Height != NULL)) {
      Status = refit_call4_wrapper(GraphicsOutput->QueryMode, GraphicsOutput, *ModeWidth, &Size, &Info);
      if ((Status == EFI_SUCCESS) && (Info != NULL)) {
         *ModeWidth = Info->HorizontalResolution;
         *Height = Info->VerticalResolution;
         return TRUE;
      }
   }
   return FALSE;
} // BOOLEAN egGetResFromMode()

// Sets the screen resolution to the specified value, if possible. If *ScreenHeight
// is 0 and GOP mode is detected, assume that *ScreenWidth contains a GOP mode
// number rather than a horizontal resolution. If the specified resolution is not
// valid, displays a warning with the valid modes on GOP (UEFI) systems, or silently
// fails on UGA (EFI 1.x) systems. Note that this function attempts to set ANY screen
// resolution, even 0x0 or ridiculously large values.
// Upon success, returns actual screen resolution in *ScreenWidth and *ScreenHeight.
// These values are unchanged upon failure.
// Returns TRUE if successful, FALSE if not.
BOOLEAN egSetScreenSize(IN OUT UINTN *ScreenWidth, IN OUT UINTN *ScreenHeight) {
   EFI_STATUS                            Status = EFI_SUCCESS;
   EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  *Info;
   UINTN                                 Size;
   UINT32                                ModeNum = 0, CurrentModeNum;
   UINT32                                UGAWidth, UGAHeight, UGADepth, UGARefreshRate;
   BOOLEAN                               ModeSet = FALSE;

   if ((ScreenWidth == NULL) || (ScreenHeight == NULL))
      return FALSE;

   if (GraphicsOutput != NULL) { // GOP mode (UEFI)
      CurrentModeNum = GraphicsOutput->Mode->Mode;
      if (*ScreenHeight == 0) { // User specified a mode number (stored in *ScreenWidth); use it directly
         ModeNum = (UINT32) *ScreenWidth;
         if (ModeNum != CurrentModeNum) {
            ModeSet = TRUE;
         } else if (egGetResFromMode(ScreenWidth, ScreenHeight) &&
             (refit_call2_wrapper(GraphicsOutput->SetMode, GraphicsOutput, ModeNum) == EFI_SUCCESS)) {
            ModeSet = TRUE;
         }

      // User specified width & height; must find mode...
      } else {
         // Do a loop through the modes to see if the specified one is available;
         // and if so, switch to it....
         do {
            Status = refit_call4_wrapper(GraphicsOutput->QueryMode, GraphicsOutput, ModeNum, &Size, &Info);
            if ((Status == EFI_SUCCESS) && (Size >= sizeof(*Info) && (Info != NULL)) &&
                (Info->HorizontalResolution == *ScreenWidth) && (Info->VerticalResolution == *ScreenHeight) &&
                ((ModeNum == CurrentModeNum) ||
                 (refit_call2_wrapper(GraphicsOutput->SetMode, GraphicsOutput, ModeNum) == EFI_SUCCESS))) {
               ModeSet = TRUE;
            } // if
         } while ((++ModeNum < GraphicsOutput->Mode->MaxMode) && !ModeSet);
      } // if/else

      if (ModeSet) {
         egScreenWidth = *ScreenWidth;
         egScreenHeight = *ScreenHeight;
      } else {// If unsuccessful, display an error message for the user....
         SwitchToText(FALSE);
         Print(L"Error setting graphics mode %d x %d; using default mode!\nAvailable modes are:\n", *ScreenWidth, *ScreenHeight);
         ModeNum = 0;
         do {
            Status = refit_call4_wrapper(GraphicsOutput->QueryMode, GraphicsOutput, ModeNum, &Size, &Info);
            if ((Status == EFI_SUCCESS) && (Info != NULL)) {
               Print(L"Mode %d: %d x %d\n", ModeNum, Info->HorizontalResolution, Info->VerticalResolution);
               if (ModeNum == CurrentModeNum) {
                   egScreenWidth = Info->HorizontalResolution;
                   egScreenHeight = Info->VerticalResolution;
               } // if
            } // else
         } while (++ModeNum < GraphicsOutput->Mode->MaxMode);
         PauseForKey();
         SwitchToGraphics();
      } // if GOP mode (UEFI)

   } else if (UgaDraw != NULL) { // UGA mode (EFI 1.x)
      // Try to use current color depth & refresh rate for new mode. Maybe not the best choice
      // in all cases, but I don't know how to probe for alternatives....
      Status = refit_call5_wrapper(UgaDraw->GetMode, UgaDraw, &UGAWidth, &UGAHeight, &UGADepth, &UGARefreshRate);
      Status = refit_call5_wrapper(UgaDraw->SetMode, UgaDraw, *ScreenWidth, *ScreenHeight, UGADepth, UGARefreshRate);
      if (Status == EFI_SUCCESS) {
         egScreenWidth = *ScreenWidth;
         egScreenHeight = *ScreenHeight;
         ModeSet = TRUE;
      } else {
         // TODO: Find a list of supported modes and display it.
         // NOTE: Below doesn't actually appear unless we explicitly switch to text mode.
         // This is just a placeholder until something better can be done....
         Print(L"Error setting graphics mode %d x %d; unsupported mode!\n");
      } // if/else
   } // if/else if UGA mode (EFI 1.x)
   return (ModeSet);
} // BOOLEAN egSetScreenSize()

// Set a text mode.
// Returns TRUE if the mode actually changed, FALSE otherwise.
// Note that a FALSE return value can mean either an error or no change
// necessary.
BOOLEAN egSetTextMode(UINT32 RequestedMode) {
   UINTN         i = 0, Width, Height;
   EFI_STATUS    Status;
   BOOLEAN       ChangedIt = FALSE;

   if ((RequestedMode != DONT_CHANGE_TEXT_MODE) && (RequestedMode != ST->ConOut->Mode->Mode)) {
      Status = refit_call2_wrapper(ST->ConOut->SetMode, ST->ConOut, RequestedMode);
      if (Status == EFI_SUCCESS) {
         ChangedIt = TRUE;
      } else {
         SwitchToText(FALSE);
         Print(L"\nError setting text mode %d; available modes are:\n", RequestedMode);
         do {
            Status = refit_call4_wrapper(ST->ConOut->QueryMode, ST->ConOut, i, &Width, &Height);
            if (Status == EFI_SUCCESS)
               Print(L"Mode %d: %d x %d\n", i, Width, Height);
         } while (++i < ST->ConOut->Mode->MaxMode);
         Print(L"Mode %d: Use default mode\n", DONT_CHANGE_TEXT_MODE);

         PauseForKey();
         SwitchToGraphics();
      } // if/else successful change
   } // if need to change mode
   return ChangedIt;
} // BOOLEAN egSetTextMode()

CHAR16 * egScreenDescription(VOID)
{
    CHAR16 *GraphicsInfo, *TextInfo = NULL;

    GraphicsInfo = AllocateZeroPool(256 * sizeof(CHAR16));
    if (GraphicsInfo == NULL)
       return L"memory allocation error";

    if (egHasGraphics) {
        if (GraphicsOutput != NULL) {
            SPrint(GraphicsInfo, 255, L"Graphics Output (UEFI), %dx%d", egScreenWidth, egScreenHeight);
        } else if (UgaDraw != NULL) {
            GraphicsInfo = AllocateZeroPool(256 * sizeof(CHAR16));
            SPrint(GraphicsInfo, 255, L"UGA Draw (EFI 1.10), %dx%d", egScreenWidth, egScreenHeight);
        } else {
            MyFreePool(GraphicsInfo);
            MyFreePool(TextInfo);
            return L"Internal Error";
        }
        if (!AllowGraphicsMode) { // graphics-capable HW, but in text mode
           TextInfo = AllocateZeroPool(256 * sizeof(CHAR16));
           SPrint(TextInfo, 255, L"(in %dx%d text mode)", ConWidth, ConHeight);
           MergeStrings(&GraphicsInfo, TextInfo, L' ');
        }
    } else {
        SPrint(GraphicsInfo, 255, L"Text-foo console, %dx%d", ConWidth, ConHeight);
    }
    MyFreePool(TextInfo);
    return GraphicsInfo;
}

BOOLEAN egHasGraphicsMode(VOID)
{
    return egHasGraphics;
}

BOOLEAN egIsGraphicsModeEnabled(VOID)
{
    EFI_CONSOLE_CONTROL_SCREEN_MODE CurrentMode;

    if (ConsoleControl != NULL) {
        refit_call4_wrapper(ConsoleControl->GetMode, ConsoleControl, &CurrentMode, NULL, NULL);
        return (CurrentMode == EfiConsoleControlScreenGraphics) ? TRUE : FALSE;
    }

    return FALSE;
}

VOID egSetGraphicsModeEnabled(IN BOOLEAN Enable)
{
    EFI_CONSOLE_CONTROL_SCREEN_MODE CurrentMode;
    EFI_CONSOLE_CONTROL_SCREEN_MODE NewMode;

    if (ConsoleControl != NULL) {
        refit_call4_wrapper(ConsoleControl->GetMode, ConsoleControl, &CurrentMode, NULL, NULL);

        NewMode = Enable ? EfiConsoleControlScreenGraphics
                         : EfiConsoleControlScreenText;
        if (CurrentMode != NewMode)
           refit_call2_wrapper(ConsoleControl->SetMode, ConsoleControl, NewMode);
    }
}

//
// Drawing to the screen
//

VOID egClearScreen(IN EG_PIXEL *Color)
{
    EFI_UGA_PIXEL FillColor;

    if (!egHasGraphics)
        return;

    if (Color != NULL) {
       FillColor.Red   = Color->r;
       FillColor.Green = Color->g;
       FillColor.Blue  = Color->b;
    } else {
       FillColor.Red   = 0x0;
       FillColor.Green = 0x0;
       FillColor.Blue  = 0x0;
    }
    FillColor.Reserved = 0;

    if (GraphicsOutput != NULL) {
        // EFI_GRAPHICS_OUTPUT_BLT_PIXEL and EFI_UGA_PIXEL have the same
        // layout, and the header from TianoCore actually defines them
        // to be the same type.
        refit_call10_wrapper(GraphicsOutput->Blt, GraphicsOutput, (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)&FillColor, EfiBltVideoFill,
                             0, 0, 0, 0, egScreenWidth, egScreenHeight, 0);
    } else if (UgaDraw != NULL) {
        refit_call10_wrapper(UgaDraw->Blt, UgaDraw, &FillColor, EfiUgaVideoFill, 0, 0, 0, 0, egScreenWidth, egScreenHeight, 0);
    }
}

VOID egDrawImage(IN EG_IMAGE *Image, IN UINTN ScreenPosX, IN UINTN ScreenPosY)
{
    EG_IMAGE *CompImage = NULL;

    // NOTE: Weird seemingly redundant tests because some placement code can "wrap around" and
    // send "negative" values, which of course become very large unsigned ints that can then
    // wrap around AGAIN if values are added to them.....
    if (!egHasGraphics || ((ScreenPosX + Image->Width) > egScreenWidth) || ((ScreenPosY + Image->Height) > egScreenHeight) ||
        (ScreenPosX > egScreenWidth) || (ScreenPosY > egScreenHeight))
        return;

    if ((GlobalConfig.ScreenBackground == NULL) || ((Image->Width == egScreenWidth) && (Image->Height == egScreenHeight))) {
       CompImage = Image;
    } else if (GlobalConfig.ScreenBackground == Image) {
       CompImage = GlobalConfig.ScreenBackground;
    } else {
       CompImage = egCropImage(GlobalConfig.ScreenBackground, ScreenPosX, ScreenPosY, Image->Width, Image->Height);
       if (CompImage == NULL) {
          Print(L"Error! Can't crop image in egDrawImage()!\n");
          return;
       }
       egComposeImage(CompImage, Image, 0, 0);
    }

    if (GraphicsOutput != NULL) {
       refit_call10_wrapper(GraphicsOutput->Blt, GraphicsOutput, (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)CompImage->PixelData,
                            EfiBltBufferToVideo, 0, 0, ScreenPosX, ScreenPosY, CompImage->Width, CompImage->Height, 0);
    } else if (UgaDraw != NULL) {
       refit_call10_wrapper(UgaDraw->Blt, UgaDraw, (EFI_UGA_PIXEL *)CompImage->PixelData, EfiUgaBltBufferToVideo,
                            0, 0, ScreenPosX, ScreenPosY, CompImage->Width, CompImage->Height, 0);
    }
    if ((CompImage != GlobalConfig.ScreenBackground) && (CompImage != Image))
       egFreeImage(CompImage);
} /* VOID egDrawImage() */

// Display an unselected icon on the screen, so that the background image shows
// through the transparency areas. The BadgeImage may be NULL, in which case
// it's not composited in.
VOID egDrawImageWithTransparency(EG_IMAGE *Image, EG_IMAGE *BadgeImage, UINTN XPos, UINTN YPos, UINTN Width, UINTN Height) {
   EG_IMAGE *Background;

   Background = egCropImage(GlobalConfig.ScreenBackground, XPos, YPos, Width, Height);
   if (Background != NULL) {
      BltImageCompositeBadge(Background, Image, BadgeImage, XPos, YPos);
      egFreeImage(Background);
   }
} // VOID DrawImageWithTransparency()

VOID egDrawImageArea(IN EG_IMAGE *Image,
                     IN UINTN AreaPosX, IN UINTN AreaPosY,
                     IN UINTN AreaWidth, IN UINTN AreaHeight,
                     IN UINTN ScreenPosX, IN UINTN ScreenPosY)
{
    if (!egHasGraphics)
        return;

    egRestrictImageArea(Image, AreaPosX, AreaPosY, &AreaWidth, &AreaHeight);
    if (AreaWidth == 0)
        return;

    if (GraphicsOutput != NULL) {
        refit_call10_wrapper(GraphicsOutput->Blt, GraphicsOutput, (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)Image->PixelData,
                             EfiBltBufferToVideo, AreaPosX, AreaPosY, ScreenPosX, ScreenPosY, AreaWidth, AreaHeight,
                             Image->Width * 4);
    } else if (UgaDraw != NULL) {
        refit_call10_wrapper(UgaDraw->Blt, UgaDraw, (EFI_UGA_PIXEL *)Image->PixelData, EfiUgaBltBufferToVideo,
                             AreaPosX, AreaPosY, ScreenPosX, ScreenPosY, AreaWidth, AreaHeight, Image->Width * 4);
    }
}

// Display a message in the center of the screen, surrounded by a box of the
// specified color. For the moment, uses graphics calls only. (It still works
// in text mode on GOP/UEFI systems, but not on UGA/EFI 1.x systems.)
VOID egDisplayMessage(IN CHAR16 *Text, EG_PIXEL *BGColor, UINTN PositionCode) {
   UINTN BoxWidth, BoxHeight;
   static UINTN Position = 1;
   EG_IMAGE *Box;

   if ((Text != NULL) && (BGColor != NULL)) {
      egMeasureText(Text, &BoxWidth, &BoxHeight);
      BoxWidth += 14;
      BoxHeight *= 2;
      if (BoxWidth > egScreenWidth)
         BoxWidth = egScreenWidth;
      Box = egCreateFilledImage(BoxWidth, BoxHeight, FALSE, BGColor);
      egRenderText(Text, Box, 7, BoxHeight / 4, (BGColor->r + BGColor->g + BGColor->b) / 3);
      switch (PositionCode) {
          case CENTER:
              Position = (egScreenHeight - BoxHeight) / 2;
              break;
          case BOTTOM:
              Position = egScreenHeight - (BoxHeight * 2);
              break;
          case TOP:
              Position = 1;
              break;
          default: // NEXTLINE
              Position += BoxHeight + (BoxHeight / 10);
              break;
      } // switch()
      egDrawImage(Box, (egScreenWidth - BoxWidth) / 2, Position);
      if ((PositionCode == CENTER) || (Position >= egScreenHeight - (BoxHeight * 5)))
          Position = 1;
   } // if non-NULL inputs
} // VOID egDisplayMessage()

// Copy the current contents of the display into an EG_IMAGE....
// Returns pointer if successful, NULL if not.
EG_IMAGE * egCopyScreen(VOID) {
   return egCopyScreenArea(0, 0, egScreenWidth, egScreenHeight);
} // EG_IMAGE * egCopyScreen()

// Copy the current contents of the specified display area into an EG_IMAGE....
// Returns pointer if successful, NULL if not.
EG_IMAGE * egCopyScreenArea(UINTN XPos, UINTN YPos, UINTN Width, UINTN Height) {
   EG_IMAGE *Image = NULL;

   if (!egHasGraphics)
      return NULL;

   // allocate a buffer for the screen area
   Image = egCreateImage(Width, Height, FALSE);
   if (Image == NULL) {
      return NULL;
   }

   // get full screen image
   if (GraphicsOutput != NULL) {
      refit_call10_wrapper(GraphicsOutput->Blt, GraphicsOutput, (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)Image->PixelData,
                           EfiBltVideoToBltBuffer, XPos, YPos, 0, 0, Image->Width, Image->Height, 0);
   } else if (UgaDraw != NULL) {
      refit_call10_wrapper(UgaDraw->Blt, UgaDraw, (EFI_UGA_PIXEL *)Image->PixelData, EfiUgaVideoToBltBuffer,
                           XPos, YPos, 0, 0, Image->Width, Image->Height, 0);
   }
   return Image;
} // EG_IMAGE * egCopyScreenArea()

//
// Make a screenshot
//

VOID egScreenShot(VOID)
{
    EFI_STATUS      Status;
    EG_IMAGE        *Image;
    UINT8           *FileData;
    UINTN           FileDataLength;
    UINTN           Index;
    UINTN           ssNum;
    CHAR16          Filename[80];
    EFI_FILE*       BaseDir;

    Image = egCopyScreen();
    if (Image == NULL) {
       Print(L"Error: Unable to take screen shot\n");
       goto bailout_wait;
    }

    // encode as BMP
    egEncodeBMP(Image, &FileData, &FileDataLength);
    egFreeImage(Image);
    if (FileData == NULL) {
        Print(L"Error egEncodeBMP returned NULL\n");
        goto bailout_wait;
    }

    Status = egFindESP(&BaseDir);
    if (EFI_ERROR(Status))
        return;

    // Search for existing screen shot files; increment number to an unused value...
    ssNum = 001;
    do {
       SPrint(Filename, 80, L"screenshot_%03d.bmp", ssNum++);
    } while (FileExists(BaseDir, Filename));

    // save to file on the ESP
    Status = egSaveFile(BaseDir, Filename, FileData, FileDataLength);
    FreePool(FileData);
    if (CheckError(Status, L"in egSaveFile()")) {
        goto bailout_wait;
    }

    return;

    // DEBUG: switch to text mode
bailout_wait:
    egSetGraphicsModeEnabled(FALSE);
    refit_call3_wrapper(BS->WaitForEvent, 1, &ST->ConIn->WaitForKey, &Index);
}

/* EOF */
