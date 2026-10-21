#pragma once
#include "ui_model.h"
enum UiAction { NoAction=-1, NavHome=0,NavEvents=1,NavNodes=2,NavSettings=3,
  ArmTile=4,OpenDiagnostics=5,OpenTouchTest=6,GoBack=7,Previous=8,Next=9,OpenCards=10,AddCard=11,StartScan=12,CancelScan=13,
  CardRow0=14,CardRow1=15,CardBack=16,RemoveCard=17,ConfirmRemove=18,
  CardsPrevious=19,CardsNext=20,OpenWhatsApp=21,TestWhatsApp=22,NextNotificationChannel=23,DismissAlert=24 };
void uiInvalidate(int x=0,int y=0,int w=320,int h=240);
void uiTick(const UiModel& model);
bool uiBusy();
void uiInterrupt();
int uiHit(const UiModel& model,int x,int y);
