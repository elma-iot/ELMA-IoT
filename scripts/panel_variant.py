"""Select the fitted VIEWE panel independently of its shared PCB identity."""
def panel_defines(board):
    if board == 'viewe-uedx32480035e-wb-a':
        return ['BOARD_VIEWE_UEDX32480035E_WB_A', ('ESP_PANEL_DRIVERS_LCD_USE_ST7789', 1)]
    if board == 'viewe-uedx24320028e-wb-a':
        return ['BOARD_VIEWE_UEDX24320028E_WB_A', ('ESP_PANEL_DRIVERS_LCD_USE_GC9A01', 1)]
    raise ValueError('Select a VIEWE panel: 2.8-inch 240x320 or 3.5-inch 320x480')
