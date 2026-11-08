"""Qt stylesheet for the Vision Board HMI."""

APP_STYLE = """
QMainWindow {
    background: #edf0f3;
    color: #26313a;
}
QWidget {
    color: #26313a;
}
QLabel {
    color: #26313a;
    background: transparent;
}
#HeaderPanel {
    background: #f9fafb;
    border: 1px solid #ccd4dc;
    border-radius: 6px;
}
#LogoBlock {
    background: transparent;
    min-width: 46px;
    max-width: 46px;
    min-height: 46px;
    max-height: 46px;
}
#ContestBannerText {
    color: #1f4e6e;
    font-size: 16px;
    font-weight: 800;
    letter-spacing: 1px;
}
#WindowTitle {
    font-size: 21px;
    font-weight: 800;
    color: #17232d;
}
#WindowSubTitle {
    color: #667785;
    font-size: 12px;
}
#ConnectionBox {
    background: #eef2f5;
    border: 1px solid #d2dae1;
    border-radius: 5px;
}
QLineEdit, QSpinBox {
    background: #ffffff;
    border: 1px solid #bdc8d1;
    border-radius: 3px;
    padding: 6px 8px;
    color: #1d2a35;
    selection-background-color: #9fb9cc;
}
QLineEdit:focus, QSpinBox:focus {
    border: 1px solid #557d99;
}
QPushButton {
    background: #f8fafc;
    border: 1px solid #b7c2cc;
    border-radius: 3px;
    padding: 7px 14px;
    color: #1f2b35;
    font-weight: 600;
}
QPushButton:hover {
    background: #e8eef3;
    border-color: #8ea4b4;
}
QPushButton:pressed {
    background: #d8e1e8;
}
QPushButton:disabled {
    background: #edf1f4;
    color: #9aa8b2;
    border-color: #d4dce2;
}
#PrimaryButton {
    background: #3f5f74;
    border-color: #3f5f74;
    color: #ffffff;
}
#PrimaryButton:hover {
    background: #496d84;
}
#SidebarPanel {
    background: #f7f9fa;
    border: 1px solid #cbd4dc;
    border-radius: 6px;
}
#SidebarTitle {
    color: #334551;
    font-weight: 800;
    padding: 2px 4px 8px 4px;
}
#SidebarNote {
    background: #eef2f5;
    border: 1px solid #d6dee5;
    border-radius: 4px;
    color: #6a7b88;
    padding: 10px;
    line-height: 145%;
}
#NavigationList {
    background: transparent;
    border: none;
    outline: none;
}
#NavigationList::item {
    min-height: 38px;
    padding-left: 12px;
    border-radius: 4px;
    color: #3a4c58;
}
#NavigationList::item:hover {
    background: #e8eef3;
}
#NavigationList::item:selected {
    background: #dce7ee;
    color: #182631;
    border-left: 4px solid #3f5f74;
    font-weight: 800;
}
#StatusStrip {
    background: transparent;
}
#StatusCard {
    background: #ffffff;
    border: 1px solid #c7d1da;
    border-left: 3px solid #3f5f74;
    border-radius: 5px;
}
#CardTitle {
    color: #687885;
    font-size: 12px;
    background: transparent;
}
#CardValue {
    color: #152330;
    font-size: 23px;
    font-weight: 800;
    background: transparent;
}
#CardSub {
    color: #6f7f8b;
    font-size: 12px;
    background: transparent;
}
#ContentStack {
    background: transparent;
}
#PageScroll {
    background: transparent;
    border: none;
}
#PageScroll > QWidget > QWidget {
    background: transparent;
}
#PageScroll QScrollBar:vertical {
    background: #edf0f3;
    width: 12px;
    margin: 0;
}
#PageScroll QScrollBar::handle:vertical {
    background: #b7c5cf;
    border-radius: 5px;
    min-height: 42px;
}
#PageScroll QScrollBar::add-line:vertical,
#PageScroll QScrollBar::sub-line:vertical {
    height: 0;
}
#PagePanel {
    background: #f9fafb;
    border: 1px solid #cbd4dc;
    border-radius: 6px;
}
#PageTitle {
    color: #192833;
    font-size: 18px;
    font-weight: 800;
}
#PageSubTitle {
    color: #667785;
    font-size: 12px;
}
#MetricPanel {
    background: #ffffff;
    border: 1px solid #c8d2db;
    border-top: 2px solid #3f5f74;
    border-radius: 5px;
}
#MetricTitle {
    color: #687885;
    font-size: 12px;
    background: transparent;
}
#MetricValue {
    color: #172633;
    font-size: 26px;
    font-weight: 800;
    background: transparent;
}
#MetricSub {
    color: #6d7d89;
    font-size: 12px;
    background: transparent;
}
QGroupBox {
    background: #ffffff;
    border: 1px solid #c8d2db;
    border-radius: 5px;
    margin-top: 12px;
    font-weight: 800;
    color: #1f2c36;
}
QGroupBox::title {
    subcontrol-origin: margin;
    left: 12px;
    padding: 0 6px;
    background: #f9fafb;
}
#FieldName {
    color: #60717f;
    background: transparent;
    padding: 2px 0;
}
#FieldValue {
    color: #1a2833;
    font-weight: 650;
    background: transparent;
    padding: 2px 0;
}
QPlainTextEdit {
    background: #ffffff;
    border: 1px solid #c7d1da;
    border-radius: 5px;
    color: #25323d;
    selection-background-color: #b9cedc;
    font-family: Consolas, "Cascadia Mono", monospace;
    font-size: 10pt;
}
#ImagePlaceholder {
    background: #ffffff;
    border: 1px dashed #aab7c2;
    border-radius: 5px;
}
#ImagePlaceholderText, #InfoNote {
    color: #687885;
}
#ButtonStrip {
    background: transparent;
}
#GatePanel {
    background: transparent;
}
#ParamTitle {
    color: #1f2c36;
    font-size: 14px;
    font-weight: 800;
    padding: 0 0 2px 2px;
}
#GateOptionStrip {
    background: #ffffff;
    border: 1px solid #c8d2db;
    border-radius: 5px;
}
QCheckBox {
    color: #26313a;
    spacing: 8px;
    font-weight: 650;
}
QCheckBox::indicator {
    width: 16px;
    height: 16px;
}
#ParamPanel {
    background: #ffffff;
    border: 1px solid #c8d2db;
    border-radius: 5px;
}
#ParamLabel {
    color: #60717f;
    min-width: 92px;
}
#ParamSpin {
    min-width: 108px;
    max-width: 150px;
}
#GateHint {
    background: #eef3f6;
    border: 1px solid #d1dce3;
    border-radius: 4px;
    color: #536572;
    padding: 12px 14px;
    line-height: 145%;
    min-height: 52px;
}
"""
