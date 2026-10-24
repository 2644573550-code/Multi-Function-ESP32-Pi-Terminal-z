.pragma library

var english = {
    "网页滚动方向轮盘": "Web page scrolling joystick",
    "按住拖动或按方向键，松开停止；Escape 停止": "Drag or hold an arrow key to scroll; release or press Escape to stop",
    "上": "Up", "下": "Down", "左": "Left", "右": "Right",
    "网页摇杆": "Web joystick", "放大网页": "Zoom in", "缩小网页": "Zoom out",
    "原生网页方向轮盘": "Native web joystick"
}

function t(text, language) {
    return language === "en" ? (english[text] || text) : text
}
