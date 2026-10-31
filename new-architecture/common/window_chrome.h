#pragma once
class QWidget;
// Call before the first show. Shared by Windows login and main windows.
void attachWindowChrome(QWidget *window);
