#!/usr/bin/env python3
"""
OTA Server - PyQt5版本
用于通过MQTT向设备推送固件升级
支持HEX和BIN文件格式
"""

import sys
import os
import paho.mqtt.client as mqtt
import intelhex
import base64
import json
import time
import struct
import hashlib
import socket

try:
    from PyQt5 import QtWidgets, QtCore, QtGui
    from PyQt5.QtWidgets import (QApplication, QMainWindow, QFileDialog, QMessageBox, QTextEdit,
                                 QLabel, QLineEdit, QPushButton, QProgressBar, QGroupBox,
                                 QVBoxLayout, QHBoxLayout, QGridLayout, QFormLayout, QTabWidget,
                                 QListWidget, QListWidgetItem, QSplitter, QWidget,
                                 QCheckBox, QComboBox, QSpinBox, QAction, QMenu)
    from PyQt5.QtCore import QTimer, Qt, QSettings, QThread, pyqtSignal, pyqtSlot
    from PyQt5.QtGui import QFont, QTextCursor, QIcon, QPalette, QColor
except ImportError:
    print("Error: PyQt5 not installed!")
    print("Please run: pip install PyQt5 paho-mqtt intelhex")
    sys.exit(1)

CLIENT_ID = "ota_server_001"
CHUNK_SIZE = 4 * 1024


class FirmwareLoader:
    """固件加载器 - 支持HEX和BIN格式"""

    @staticmethod
    def load_hex(file_path):
        """加载HEX文件"""
        ih = intelhex.IntelHex(file_path)
        return ih.tobinstr()

    @staticmethod
    def load_bin(file_path):
        """加载BIN文件"""
        with open(file_path, 'rb') as f:
            return f.read()

    @staticmethod
    def load_file(file_path):
        """根据扩展名自动选择加载方式"""
        _, ext = os.path.splitext(file_path)
        ext = ext.lower()

        if ext == '.hex':
            return FirmwareLoader.load_hex(file_path)
        elif ext == '.bin':
            return FirmwareLoader.load_bin(file_path)
        else:
            raise ValueError(f"Unsupported file format: {ext}")


class CRC32Calculator:
    """CRC32计算器"""

    @staticmethod
    def calculate(data):
        crc = 0xFFFFFFFF
        table = CRC32Calculator._make_table()
        for byte in data:
            crc = (crc >> 8) ^ table[(crc ^ byte) & 0xFF]
        return crc ^ 0xFFFFFFFF

    @staticmethod
    def _make_table():
        table = []
        for i in range(256):
            crc = i
            for _ in range(8):
                if crc & 1:
                    crc = (crc >> 1) ^ 0xEDB88320
                else:
                    crc >>= 1
            table.append(crc)
        return table


class MQTTWorker(QThread):
    """MQTT工作线程"""
    connected = pyqtSignal()
    disconnected = pyqtSignal()
    message_received = pyqtSignal(str, str)
    error_occurred = pyqtSignal(str)
    log_message = pyqtSignal(str)

    def __init__(self, broker_address, broker_port, client_id):
        super().__init__()
        self.broker_address = broker_address
        self.broker_port = broker_port
        self.client_id = client_id
        self.mqtt_client = None
        self.running = True

    def run(self):
        self.log_message.emit(f"[MQTT-WORKER] 线程启动，客户端ID: {self.client_id}")

        # 使用回调API版本2以兼容paho-mqtt 2.0+
        self.log_message.emit(f"[MQTT-WORKER] 创建MQTT客户端，使用API版本2")
        self.mqtt_client = mqtt.Client(client_id=self.client_id, callback_api_version=mqtt.CallbackAPIVersion.VERSION2)

        # 启用调试日志
        self.mqtt_client.enable_logger()

        self.mqtt_client.on_connect = self.on_connect
        self.mqtt_client.on_disconnect = self.on_disconnect
        self.mqtt_client.on_message = self.on_message
        self.mqtt_client.on_log = self.on_log

        try:
            self.log_message.emit(f"[MQTT-WORKER] 尝试连接到 {self.broker_address}:{self.broker_port}")
            self.log_message.emit(f"[MQTT-WORKER] 连接超时设置: 60秒")

            # 尝试连接
            self.mqtt_client.connect(self.broker_address, self.broker_port, 60)

            self.log_message.emit(f"[MQTT-WORKER] 连接成功，进入消息循环")
            while self.running:
                try:
                    self.mqtt_client.loop(timeout=1.0)
                except Exception as loop_e:
                    self.log_message.emit(f"[MQTT-WORKER] 消息循环异常: {str(loop_e)}")
                    break
        except ConnectionRefusedError:
            self.log_message.emit(f"[MQTT-WORKER] 连接被拒绝 - Broker可能未运行或端口不正确")
            self.run_network_diagnostics()
            self.error_occurred.emit(f"连接被拒绝，请检查Broker地址和端口是否正确")
        except TimeoutError:
            self.log_message.emit(f"[MQTT-WORKER] 连接超时 - 无法连接到 {self.broker_address}:{self.broker_port}")
            self.run_network_diagnostics()
            self.error_occurred.emit(f"连接超时，请检查网络连接和Broker地址")
        except Exception as e:
            self.log_message.emit(f"[MQTT-WORKER] 连接异常: {type(e).__name__} - {str(e)}")
            self.run_network_diagnostics()
            self.error_occurred.emit(f"连接错误: {type(e).__name__} - {str(e)}")

    def run_network_diagnostics(self):
        """运行网络诊断"""
        self.log_message.emit(f"[DIAGNOSTICS] 开始网络诊断...")

        # 1. 尝试解析域名
        try:
            ip_address = socket.gethostbyname(self.broker_address)
            self.log_message.emit(f"[DIAGNOSTICS] DNS解析成功: {self.broker_address} -> {ip_address}")
        except socket.gaierror as e:
            self.log_message.emit(f"[DIAGNOSTICS] DNS解析失败: {str(e)}")
            return

        # 2. 尝试TCP连接测试
        self.log_message.emit(f"[DIAGNOSTICS] 尝试TCP连接到 {ip_address}:{self.broker_port}")
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(10)
        try:
            result = sock.connect_ex((ip_address, self.broker_port))
            if result == 0:
                self.log_message.emit(f"[DIAGNOSTICS] TCP连接成功")
            else:
                self.log_message.emit(f"[DIAGNOSTICS] TCP连接失败，错误码: {result}")
                self.log_message.emit(f"[DIAGNOSTICS] 可能的原因:")
                self.log_message.emit(f"[DIAGNOSTICS]   - MQTT Broker未在 {ip_address}:{self.broker_port} 运行")
                self.log_message.emit(f"[DIAGNOSTICS]   - 防火墙阻止了 {self.broker_port} 端口")
                self.log_message.emit(f"[DIAGNOSTICS]   - 网络路由问题")
        except Exception as e:
            self.log_message.emit(f"[DIAGNOSTICS] TCP连接测试异常: {str(e)}")
        finally:
            sock.close()

        # 3. 检查本地网络
        try:
            hostname = socket.gethostname()
            local_ip = socket.gethostbyname(hostname)
            self.log_message.emit(f"[DIAGNOSTICS] 本地主机名: {hostname}")
            self.log_message.emit(f"[DIAGNOSTICS] 本地IP地址: {local_ip}")
        except Exception as e:
            self.log_message.emit(f"[DIAGNOSTICS] 获取本地信息失败: {str(e)}")

        self.log_message.emit(f"[DIAGNOSTICS] 诊断完成")

    def on_connect(self, client, userdata, flags, rc, properties):
        # paho-mqtt 2.0+ 使用 ReasonCode 对象，需要通过 .value 属性获取整数值
        rc_int = rc.value if hasattr(rc, 'value') else rc
        self.log_message.emit(f"[MQTT-WORKER] on_connect 回调触发，返回码: {rc} (类型: {type(rc).__name__}, 整数: {rc_int})")

        rc_messages = {
            0: "连接成功",
            1: "协议版本错误",
            2: "无效的客户端ID",
            3: "服务器不可用",
            4: "用户名或密码错误",
            5: "未授权"
        }

        if rc_int == 0:
            self.log_message.emit(f"[MQTT-WORKER] {rc_messages.get(rc_int, f'未知返回码: {rc}')}")
            self.connected.emit()
        else:
            self.log_message.emit(f"[MQTT-WORKER] 连接失败: {rc_messages.get(rc_int, f'未知返回码: {rc}')}")
            self.error_occurred.emit(f"连接失败，错误码: {rc_int} - {rc_messages.get(rc_int, '未知错误')}")

    def on_disconnect(self, client, userdata, rc, properties, reason_code):
        # paho-mqtt 2.0+ 使用 ReasonCode 对象
        rc_int = rc.value if hasattr(rc, 'value') else rc if rc is not None else 0
        self.log_message.emit(f"[MQTT-WORKER] on_disconnect 回调触发，返回码: {rc} (整数: {rc_int})")
        if rc_int != 0:
            self.log_message.emit(f"[MQTT-WORKER] 意外断开连接，错误码: {rc}")
        self.disconnected.emit()

    def on_message(self, client, userdata, msg):
        payload = msg.payload.decode()
        self.log_message.emit(f"[MQTT-WORKER] 收到消息: {msg.topic} -> {payload[:100]}..." if len(payload) > 100 else f"[MQTT-WORKER] 收到消息: {msg.topic} -> {payload}")
        self.message_received.emit(msg.topic, payload)

    def on_log(self, client, userdata, level, buf):
        self.log_message.emit(f"[MQTT-LOG] {buf}")

    def subscribe(self, topic, qos=1):
        if self.mqtt_client:
            self.log_message.emit(f"[MQTT-WORKER] 订阅主题: {topic} (QoS: {qos})")
            self.mqtt_client.subscribe(topic, qos)

    def publish(self, topic, payload, qos=1):
        if self.mqtt_client:
            self.mqtt_client.publish(topic, payload, qos)

    def stop(self):
        self.log_message.emit(f"[MQTT-WORKER] 停止请求收到")
        self.running = False
        if self.mqtt_client:
            self.log_message.emit(f"[MQTT-WORKER] 断开MQTT连接")
            self.mqtt_client.disconnect()


class OTAServer(QMainWindow):
    def __init__(self):
        super().__init__()

        # 默认配置 - 使用EMQX公共MQTT服务器
        self.broker_address = "broker.emqx.io"
        self.broker_port = 1883
        self.device_id = "w5500_001"

        # 状态变量
        self.firmware_data = None
        self.firmware_size = 0
        self.total_chunks = 0
        self.sent_chunks = 0
        self.transfer_in_progress = False
        self.current_version = "1.0.0"
        self.mqtt_worker = None
        self.lbl_status_icon = None
        self.lbl_broker_info = None
        self.lbl_status_icon = None
        self.lbl_broker_info = None

        # UI组件
        self.tabs = None
        self.le_broker_address = None
        self.le_broker_port = None
        self.le_device_id = None
        self.btn_connect = None
        self.btn_disconnect = None
        self.lbl_status = None
        self.le_firmware_path = None
        self.le_version = None
        self.progress_bar = None
        self.lbl_progress = None
        self.te_log = None
        self.list_devices = None

        self.initUI()
        self.loadSettings()
        self.append_log("[INFO] W5500 OTA 升级服务器已启动")
        self.append_log("[INFO] 等待连接到MQTT Broker...")

    def initUI(self):
        # 窗口设置
        self.setWindowTitle("W5500 OTA 升级服务器")
        self.setGeometry(100, 100, 1000, 750)
        self.setWindowIcon(self.create_icon())

        # 主布局 - 水平布局
        central_widget = QtWidgets.QWidget()
        self.setCentralWidget(central_widget)
        main_layout = QtWidgets.QHBoxLayout(central_widget)

        # 左侧面板
        left_panel = QtWidgets.QWidget()
        left_panel.setFixedWidth(220)
        left_layout = QtWidgets.QVBoxLayout(left_panel)

        # 右侧面板（垂直布局：标签页在上，日志在下）
        right_panel = QtWidgets.QWidget()
        right_layout = QtWidgets.QVBoxLayout(right_panel)
        right_layout.setSpacing(0)

        main_layout.addWidget(left_panel)
        main_layout.addWidget(right_panel)

        # ===== 左侧面板 =====
        # 连接状态 - 圆形指示器卡片
        status_group = QGroupBox("连接状态")
        status_layout = QVBoxLayout(status_group)
        status_layout.setContentsMargins(8, 4, 8, 8)
        status_layout.setSpacing(8)
        status_layout.setAlignment(Qt.AlignCenter)

        indicator_container = QWidget()
        indicator_container.setFixedSize(88, 88)
        indicator_layout = QHBoxLayout(indicator_container)
        indicator_layout.setContentsMargins(0, 0, 0, 0)
        indicator_layout.setAlignment(Qt.AlignCenter)

        self.status_indicator = QtGui.QPixmap(88, 88)
        self._draw_status_indicator(False)
        self.lbl_status_icon = QLabel()
        self.lbl_status_icon.setPixmap(self.status_indicator)
        self.lbl_status_icon.setAlignment(Qt.AlignCenter)
        self.lbl_status_icon.setFixedSize(88, 88)
        indicator_layout.addWidget(self.lbl_status_icon)

        status_layout.addWidget(indicator_container, alignment=Qt.AlignCenter)

        self.lbl_status = QLabel()
        self.lbl_status.setAlignment(Qt.AlignCenter)
        font_status = QFont("Segoe UI", 14, QFont.Bold)
        self.lbl_status.setFont(font_status)
        self.lbl_status.setStyleSheet("color: #E74C3C; background: transparent;")
        status_layout.addWidget(self.lbl_status)

        self.lbl_broker_info = QLabel()
        self.lbl_broker_info.setAlignment(Qt.AlignCenter)
        font_detail = QFont("Segoe UI", 9)
        self.lbl_broker_info.setFont(font_detail)
        self.lbl_broker_info.setStyleSheet("color: #666666; background: transparent;")
        self.lbl_broker_info.setText("默认配置")
        status_layout.addWidget(self.lbl_broker_info)

        self.update_status("未连接", False)
        left_layout.addWidget(status_group)

        # 设备列表
        device_group = QGroupBox("设备列表")
        device_layout = QVBoxLayout(device_group)

        self.list_devices = QListWidget()
        self.list_devices.addItems(["w5500_001", "w5500_002", "w5500_003", "w5500_004"])
        self.list_devices.itemClicked.connect(self.on_device_selected)
        device_layout.addWidget(self.list_devices)

        refresh_btn = QPushButton("刷新")
        refresh_btn.clicked.connect(self.refresh_devices)
        device_layout.addWidget(refresh_btn)

        left_layout.addWidget(device_group)
        left_layout.addStretch()

        # ===== 右侧面板 =====
        # 标签页（连接配置和OTA升级）
        self.tabs = QTabWidget()

        # 连接配置
        conn_tab = QWidget()
        conn_layout = QVBoxLayout(conn_tab)
        self.create_connection_panel(conn_layout)
        self.tabs.addTab(conn_tab, "连接配置")

        # OTA升级
        ota_tab = QWidget()
        ota_layout = QVBoxLayout(ota_tab)
        self.create_ota_panel(ota_layout)
        self.tabs.addTab(ota_tab, "OTA升级")

        right_layout.addWidget(self.tabs)

        # 日志面板（始终显示在底部）
        log_group = QGroupBox("日志")
        log_layout = QVBoxLayout(log_group)
        self.create_log_panel(log_layout)
        right_layout.addWidget(log_group)

        # 工具栏
        self.create_toolbar()

        # 状态栏
        self.statusBar().showMessage("就绪")

    def create_icon(self):
        """创建窗口图标"""
        pixmap = QtGui.QPixmap(32, 32)
        pixmap.fill(Qt.blue)
        painter = QtGui.QPainter(pixmap)
        painter.setPen(Qt.white)
        painter.setFont(QFont("Arial", 16, QFont.Bold))
        painter.drawText(pixmap.rect(), Qt.AlignCenter, "O")
        painter.end()
        return QIcon(pixmap)

    def create_toolbar(self):
        """创建工具栏"""
        toolbar = self.addToolBar("OTA工具栏")
        toolbar.setMovable(False)

        # 连接动作
        connect_action = QAction(QIcon.fromTheme("network-connect"), "连接", self)
        connect_action.triggered.connect(self.on_connect_clicked)
        toolbar.addAction(connect_action)

        disconnect_action = QAction(QIcon.fromTheme("network-disconnect"), "断开", self)
        disconnect_action.triggered.connect(self.on_disconnect_clicked)
        toolbar.addAction(disconnect_action)

        toolbar.addSeparator()

        # 文件动作
        browse_action = QAction(QIcon.fromTheme("file-open"), "浏览固件", self)
        browse_action.triggered.connect(self.on_browse_clicked)
        toolbar.addAction(browse_action)

        toolbar.addSeparator()

        # 升级动作
        upgrade_action = QAction(QIcon.fromTheme("system-software-update"), "开始升级", self)
        upgrade_action.triggered.connect(self.on_start_upgrade_clicked)
        toolbar.addAction(upgrade_action)

        toolbar.addSeparator()

        # 日志动作
        clear_log_action = QAction(QIcon.fromTheme("edit-clear"), "清除日志", self)
        clear_log_action.triggered.connect(self.te_log.clear)
        toolbar.addAction(clear_log_action)

    def create_connection_panel(self, parent_layout):
        """创建连接配置面板"""
        group = QGroupBox("MQTT Broker 设置")
        layout = QFormLayout(group)

        # Broker地址
        self.le_broker_address = QLineEdit()
        self.le_broker_address.setPlaceholderText("Broker地址")
        layout.addRow("Broker:", self.le_broker_address)

        # 端口
        self.le_broker_port = QLineEdit()
        self.le_broker_port.setPlaceholderText("1883")
        self.le_broker_port.setValidator(QtGui.QIntValidator(1, 65535))
        layout.addRow("端口:", self.le_broker_port)

        # 设备ID
        self.le_device_id = QLineEdit()
        self.le_device_id.setPlaceholderText("设备ID")
        layout.addRow("设备ID:", self.le_device_id)

        # 按钮
        btn_layout = QHBoxLayout()
        self.btn_connect = QPushButton("连接")
        self.btn_connect.clicked.connect(self.on_connect_clicked)
        btn_layout.addWidget(self.btn_connect)

        self.btn_disconnect = QPushButton("断开")
        self.btn_disconnect.clicked.connect(self.on_disconnect_clicked)
        self.btn_disconnect.setEnabled(False)
        btn_layout.addWidget(self.btn_disconnect)

        reset_btn = QPushButton("重置配置")
        reset_btn.clicked.connect(self.reset_settings)
        btn_layout.addWidget(reset_btn)

        layout.addRow("", btn_layout)

        # 高级设置
        adv_group = QGroupBox("高级选项")
        adv_layout = QVBoxLayout(adv_group)

        # 保持连接
        self.cb_keepalive = QCheckBox("启用保持连接")
        self.cb_keepalive.setChecked(True)
        adv_layout.addWidget(self.cb_keepalive)

        self.sb_keepalive = QSpinBox()
        self.sb_keepalive.setRange(10, 300)
        self.sb_keepalive.setValue(60)
        adv_layout.addWidget(QLabel("保持连接间隔 (秒):"))
        adv_layout.addWidget(self.sb_keepalive)

        layout.addRow(adv_group)

        parent_layout.addWidget(group)
        parent_layout.addStretch()

    def create_ota_panel(self, parent_layout):
        """创建OTA升级面板"""
        # 固件选择
        fw_group = QGroupBox("固件选择")
        fw_layout = QGridLayout(fw_group)

        fw_layout.addWidget(QLabel("固件文件:"), 0, 0)
        self.le_firmware_path = QLineEdit()
        self.le_firmware_path.setReadOnly(True)
        self.le_firmware_path.setPlaceholderText("选择固件文件...")
        fw_layout.addWidget(self.le_firmware_path, 0, 1, 1, 3)

        browse_btn = QPushButton("浏览")
        browse_btn.clicked.connect(self.on_browse_clicked)
        fw_layout.addWidget(browse_btn, 0, 4)

        # 版本信息
        fw_layout.addWidget(QLabel("固件版本:"), 1, 0)
        self.le_version = QLineEdit()
        self.le_version.setPlaceholderText("1.0.0")
        fw_layout.addWidget(self.le_version, 1, 1)

        # 文件信息
        self.lbl_file_info = QLabel()
        fw_layout.addWidget(self.lbl_file_info, 1, 2, 1, 3)

        parent_layout.addWidget(fw_group)

        # 升级配置
        config_group = QGroupBox("升级配置")
        config_layout = QFormLayout(config_group)

        # 分块大小
        self.cb_chunk_size = QComboBox()
        self.cb_chunk_size.addItems(["1KB", "2KB", "4KB", "8KB"])
        self.cb_chunk_size.setCurrentIndex(2)  # 4KB默认
        config_layout.addRow("分块大小:", self.cb_chunk_size)

        # 传输延迟
        self.sb_delay = QSpinBox()
        self.sb_delay.setRange(10, 500)
        self.sb_delay.setValue(50)
        config_layout.addRow("传输延迟 (毫秒):", self.sb_delay)

        # 自动重试
        self.cb_auto_retry = QCheckBox("失败自动重试")
        self.cb_auto_retry.setChecked(True)
        config_layout.addWidget(self.cb_auto_retry)

        parent_layout.addWidget(config_group)

        # 进度显示
        progress_group = QGroupBox("传输进度")
        progress_layout = QVBoxLayout(progress_group)

        self.lbl_progress = QLabel("0 / 0 块")
        self.lbl_progress.setAlignment(Qt.AlignCenter)
        progress_layout.addWidget(self.lbl_progress)

        self.progress_bar = QProgressBar()
        self.progress_bar.setValue(0)
        self.progress_bar.setStyleSheet("""
            QProgressBar {
                border: 2px solid #2196F3;
                border-radius: 5px;
                text-align: center;
            }
            QProgressBar::chunk {
                background-color: #2196F3;
                border-radius: 3px;
            }
        """)
        progress_layout.addWidget(self.progress_bar)

        parent_layout.addWidget(progress_group)

        # 控制按钮
        btn_layout = QHBoxLayout()

        self.btn_start_upgrade = QPushButton("开始升级")
        self.btn_start_upgrade.clicked.connect(self.on_start_upgrade_clicked)
        self.btn_start_upgrade.setEnabled(False)
        self.btn_start_upgrade.setStyleSheet("""
            QPushButton {
                background-color: #4CAF50;
                color: white;
                border: none;
                padding: 10px 24px;
                border-radius: 4px;
                font-weight: bold;
            }
            QPushButton:hover {
                background-color: #45a049;
            }
            QPushButton:disabled {
                background-color: #cccccc;
            }
        """)
        btn_layout.addWidget(self.btn_start_upgrade)

        self.btn_stop_upgrade = QPushButton("停止升级")
        self.btn_stop_upgrade.clicked.connect(self.on_stop_upgrade_clicked)
        self.btn_stop_upgrade.setEnabled(False)
        self.btn_stop_upgrade.setStyleSheet("""
            QPushButton {
                background-color: #f44336;
                color: white;
                border: none;
                padding: 10px 24px;
                border-radius: 4px;
                font-weight: bold;
            }
            QPushButton:hover {
                background-color: #da190b;
            }
        """)
        btn_layout.addWidget(self.btn_stop_upgrade)

        parent_layout.addLayout(btn_layout)
        parent_layout.addStretch()

    def create_log_panel(self, parent_layout):
        """创建日志面板"""
        self.te_log = QTextEdit()
        self.te_log.setReadOnly(True)
        self.te_log.setFont(QFont("Consolas", 9))
        self.te_log.setStyleSheet("background-color: #1e1e1e; color: #d4d4d4;")
        parent_layout.addWidget(self.te_log)

        # 日志控制
        ctrl_layout = QHBoxLayout()

        self.cb_auto_scroll = QCheckBox("自动滚动")
        self.cb_auto_scroll.setChecked(True)
        ctrl_layout.addWidget(self.cb_auto_scroll)

        self.cb_timestamp = QCheckBox("显示时间戳")
        self.cb_timestamp.setChecked(True)
        ctrl_layout.addWidget(self.cb_timestamp)

        clear_btn = QPushButton("清除日志")
        clear_btn.clicked.connect(self.te_log.clear)
        ctrl_layout.addWidget(clear_btn)

        save_btn = QPushButton("保存日志")
        save_btn.clicked.connect(self.save_log)
        ctrl_layout.addWidget(save_btn)

        parent_layout.addLayout(ctrl_layout)

    def loadSettings(self):
        """加载保存的配置"""
        settings = QSettings("OTA_Server", "W5500")
        self.le_broker_address.setText(settings.value("brokerAddress", self.broker_address))
        self.le_broker_port.setText(settings.value("brokerPort", str(self.broker_port)))
        self.le_device_id.setText(settings.value("deviceId", self.device_id))
        self.le_version.setText(settings.value("version", "1.0.0"))

    def saveSettings(self):
        """保存配置"""
        settings = QSettings("OTA_Server", "W5500")
        settings.setValue("brokerAddress", self.le_broker_address.text())
        settings.setValue("brokerPort", self.le_broker_port.text())
        settings.setValue("deviceId", self.le_device_id.text())
        settings.setValue("version", self.le_version.text())

    def reset_settings(self):
        """重置配置为默认值"""
        settings = QSettings("OTA_Server", "W5500")
        settings.clear()

        # 重置为默认值
        self.broker_address = "broker.emqx.io"
        self.broker_port = 1883
        self.device_id = "w5500_001"

        # 更新UI
        self.le_broker_address.setText(self.broker_address)
        self.le_broker_port.setText(str(self.broker_port))
        self.le_device_id.setText(self.device_id)

        self.append_log("[INFO] 配置已重置为默认值")
        self.append_log(f"[INFO] 默认Broker: {self.broker_address}:{self.broker_port}")

    def update_status(self, text, connected):
        """更新连接状态显示"""
        if connected:
            self.lbl_status.setText("<span style='color:#2ECC71;'>" + text + "</span>")
            self._draw_status_indicator(True)
            self.lbl_status_icon.setPixmap(self.status_indicator)
        else:
            self.lbl_status.setText("<span style='color:#E74C3C;'>" + text + "</span>")
            self._draw_status_indicator(False)
            self.lbl_status_icon.setPixmap(self.status_indicator)


    def _draw_status_indicator(self, connected):
        """绘制简单的圆形状态指示器
        - connected=True: 绿色圆形
        - connected=False: 红色圆形
        """
        pix = self.status_indicator
        pix.fill(Qt.transparent)
        painter = QtGui.QPainter(pix)
        painter.setRenderHint(QtGui.QPainter.Antialiasing)

        cx = 44; cy = 44; r = 36

        if connected:
            # 已连接：绿色
            fill_color = QtGui.QColor(76, 175, 80)  # #4CAF50 绿色
        else:
            # 未连接：红色
            fill_color = QtGui.QColor(244, 67, 54)  # #F44336 红色

        # 绘制圆形
        painter.setBrush(fill_color)
        painter.setPen(Qt.NoPen)
        painter.drawEllipse(cx - r, cy - r, r * 2, r * 2)

        painter.end()

    def on_device_selected(self, item):
        """设备选择事件"""
        self.device_id = item.text()
        self.le_device_id.setText(self.device_id)
        self.append_log(f"[INFO] 已选择设备: {self.device_id}")

    def refresh_devices(self):
        """刷新设备列表"""
        self.append_log("[INFO] 正在刷新设备列表...")

    def on_connect_clicked(self):
        """连接按钮事件"""
        self.broker_address = self.le_broker_address.text().strip()
        self.broker_port = int(self.le_broker_port.text().strip()) if self.le_broker_port.text().strip() else 1883
        self.device_id = self.le_device_id.text().strip()
        self.lbl_broker_info.setText(self.broker_address + ":" + str(self.broker_port))

        if not self.broker_address:
            QMessageBox.warning(self, "输入错误", "请输入Broker地址")
            return

        self.append_log(f"[MQTT] 正在连接到 {self.broker_address}:{self.broker_port}...")
        self.append_log(f"[MQTT] 目标设备ID: {self.device_id}")

        self.mqtt_worker = MQTTWorker(
            self.broker_address,
            self.broker_port,
            f"{CLIENT_ID}_{int(time.time())}"
        )
        self.mqtt_worker.connected.connect(self.on_mqtt_connected)
        self.mqtt_worker.disconnected.connect(self.on_mqtt_disconnected)
        self.mqtt_worker.message_received.connect(self.on_mqtt_message)
        self.mqtt_worker.error_occurred.connect(self.on_mqtt_error)
        self.mqtt_worker.log_message.connect(self.append_log)  # 连接工作线程日志
        self.mqtt_worker.start()

        self.btn_connect.setEnabled(False)

    def on_disconnect_clicked(self):
        """断开连接按钮事件 - 安全地断开MQTT连接"""
        # 状态检查
        if not self.mqtt_worker:
            self.append_log("[WARN] 未找到MQTT工作线程")
            QMessageBox.warning(self, "警告", "未找到MQTT工作线程")
            return

        if not self.mqtt_worker.isRunning():
            self.append_log("[WARN] MQTT工作线程未运行")
            QMessageBox.warning(self, "警告", "MQTT工作线程未运行")
            return

        # 检查是否正在进行OTA升级
        if self.transfer_in_progress:
            reply = QMessageBox.question(
                self,
                "确认断开",
                "当前正在进行OTA升级，确定要断开连接吗？",
                QMessageBox.Yes | QMessageBox.No,
                QMessageBox.No
            )
            if reply == QMessageBox.No:
                return
            else:
                # 停止升级
                self.on_stop_upgrade_clicked()

        self.append_log("[MQTT] 正在断开连接...")

        # 停止MQTT工作线程
        try:
            self.mqtt_worker.stop()
            # 等待线程结束（最多等待5秒）
            if not self.mqtt_worker.wait(5000):
                self.append_log("[WARN] MQTT工作线程超时未正常退出")

            self.append_log("[MQTT] 已成功断开连接")
        except Exception as e:
            self.append_log(f"[ERROR] 断开连接时发生错误: {str(e)}")
            QMessageBox.critical(self, "错误", f"断开连接失败: {str(e)}")

    def on_mqtt_connected(self):
        """MQTT连接成功"""
        self.update_status("已连接", True)
        self.btn_connect.setEnabled(False)
        self.btn_disconnect.setEnabled(True)
        self.lbl_broker_info.setText("已连接 " + self.broker_address + ":" + str(self.broker_port))
        self.append_log("[MQTT] 已连接到Broker")

        # 订阅相关主题
        topics = [
            f"device/{self.device_id}/ota/status",
            f"device/{self.device_id}/ota/ack",
            f"device/{self.device_id}/ota/response"
        ]

        for topic in topics:
            self.mqtt_worker.subscribe(topic)
            self.append_log(f"[MQTT] 已订阅: {topic}")

    def on_mqtt_disconnected(self):
        """MQTT断开连接"""
        self.update_status("已断开", False)
        self.lbl_broker_info.setText("已断开")
        self.btn_connect.setEnabled(True)
        self.btn_disconnect.setEnabled(False)
        self.transfer_in_progress = False
        self.append_log("[MQTT] 已从Broker断开")

    def on_mqtt_error(self, error):
        """MQTT错误处理"""
        self.update_status("错误", False)
        self.lbl_broker_info.setText("连接错误")
        self.btn_connect.setEnabled(True)
        QMessageBox.critical(self, "MQTT错误", error)
        self.append_log(f"[错误] {error}")

    def on_mqtt_message(self, topic, payload):
        """MQTT消息处理"""
        # 格式化显示收到的消息
        try:
            payload_json = json.loads(payload)
            payload_str = json.dumps(payload_json, indent=2, ensure_ascii=False)
        except:
            payload_str = payload

        self.append_log(f"┌─────────────────────────────────────────────")
        self.append_log(f"│ [RECV] 主题: {topic}")
        self.append_log(f"│ [RECV] 内容: {payload_str}")
        self.append_log(f"└─────────────────────────────────────────────")

        if "/ota/status" in topic:
            try:
                status = json.loads(payload)
                self.handle_ota_status(status)
            except Exception as e:
                self.append_log(f"[错误] 解析状态失败: {e}")

        elif "/ota/ack" in topic:
            try:
                ack = json.loads(payload)
                self.handle_ota_ack(ack)
            except Exception as e:
                self.append_log(f"[错误] 解析ACK失败: {e}")

    def handle_ota_status(self, status):
        """处理OTA状态"""
        stage = status.get('stage', -1)
        progress = status.get('progress', 0)
        error = status.get('error', 0)

        stage_names = ["空闲", "检查中", "下载中", "校验中", "安装中", "成功", "失败"]
        stage_name = stage_names[stage] if 0 <= stage < len(stage_names) else "未知"

        self.append_log(f"[OTA] 阶段: {stage_name}, 进度: {progress}%, 错误: {error}")

        if stage == 5:  # SUCCESS
            self.transfer_in_progress = False
            self.progress_bar.setValue(100)
            self.btn_start_upgrade.setEnabled(True)
            self.btn_stop_upgrade.setEnabled(False)
            QMessageBox.information(self, "成功", "固件升级完成！")
            self.append_log("=== 升级完成 ===")

        elif stage == 6:  # FAILED
            self.transfer_in_progress = False
            self.btn_start_upgrade.setEnabled(True)
            self.btn_stop_upgrade.setEnabled(False)
            QMessageBox.critical(self, "升级失败", f"OTA升级失败，错误码: {error}")
            self.append_log("=== 升级失败 ===")

    def handle_ota_ack(self, ack):
        """处理OTA确认 - 收到ACK后发送下一块"""
        index = ack.get('index', -1)
        success = ack.get('success', False)

        # 如果正在等待ACK且ACK索引匹配
        if hasattr(self, 'waiting_for_ack') and self.waiting_for_ack:
            # 停止超时定时器
            if hasattr(self, 'ack_timeout_timer'):
                self.ack_timeout_timer.stop()

            if success:
                self.append_log(f"[OTA] 块 {index} ACK成功")
                self.waiting_for_ack = False

                # 更新进度
                self.sent_chunks += 1
                progress = int((self.sent_chunks * 100) / self.total_chunks)
                self.progress_bar.setValue(progress)
                self.lbl_progress.setText(f"{self.sent_chunks} / {self.total_chunks} 块")

                # 发送下一块（延迟一段时间）
                delay = self.sb_delay.value()
                QTimer.singleShot(delay, self.send_next_chunk)
            else:
                self.append_log(f"[OTA] 块 {index} ACK失败，重新发送...")
                self.waiting_for_ack = False
                # 重新发送当前块（sent_chunks不变）
                QTimer.singleShot(100, self.send_next_chunk)
        else:
            self.append_log(f"[OTA] 收到块 {index} ACK: {success}")

    def on_browse_clicked(self):
        """浏览文件按钮事件"""
        file_path, _ = QFileDialog.getOpenFileName(
            self,
            "选择固件文件",
            "",
            "固件文件 (*.hex *.bin);;HEX文件 (*.hex);;BIN文件 (*.bin);;所有文件 (*.*)"
        )

        if file_path:
            self.le_firmware_path.setText(file_path)
            if self.load_firmware(file_path):
                self.btn_start_upgrade.setEnabled(True)
            else:
                self.btn_start_upgrade.setEnabled(False)

    def load_firmware(self, file_path):
        """加载固件文件"""
        try:
            self.append_log(f"[OTA] 正在加载固件: {os.path.basename(file_path)}")

            self.firmware_data = FirmwareLoader.load_file(file_path)
            self.firmware_size = len(self.firmware_data)

            chunk_size_text = self.cb_chunk_size.currentText()
            chunk_size_kb = int(chunk_size_text.replace("KB", ""))
            global CHUNK_SIZE
            CHUNK_SIZE = chunk_size_kb * 1024

            self.total_chunks = (self.firmware_size + CHUNK_SIZE - 1) // CHUNK_SIZE

            crc32 = CRC32Calculator.calculate(self.firmware_data)
            md5 = hashlib.md5(self.firmware_data).hexdigest()

            file_info = f"大小: {self.firmware_size} 字节 | 块数: {self.total_chunks} | CRC32: {hex(crc32)[2:].upper()} | MD5: {md5[:16]}..."
            self.lbl_file_info.setText(file_info)

            self.append_log(f"[OTA] 固件已加载: {self.firmware_size} 字节")
            self.append_log(f"[OTA] 总分块: {self.total_chunks} (大小: {CHUNK_SIZE} 字节)")
            self.append_log(f"[OTA] CRC32: {hex(crc32)[2:].upper()}")
            self.append_log(f"[OTA] MD5: {md5}")

            return True
        except Exception as e:
            QMessageBox.critical(self, "文件错误", f"加载固件失败: {str(e)}")
            self.append_log(f"[错误] 加载固件失败: {str(e)}")
            return False

    def on_start_upgrade_clicked(self):
        """开始升级按钮事件"""
        version = self.le_version.text().strip()

        if not version:
            QMessageBox.warning(self, "输入错误", "请输入固件版本")
            return

        if not self.mqtt_worker or not self.mqtt_worker.isRunning():
            QMessageBox.warning(self, "连接错误", "未连接到MQTT Broker")
            return

        if not self.firmware_data:
            QMessageBox.warning(self, "固件错误", "未加载固件")
            return

        self.append_log(f"[OTA] 正在升级到版本 {version}...")
        self.sent_chunks = 0
        self.transfer_in_progress = True

        self.btn_start_upgrade.setEnabled(False)
        self.btn_stop_upgrade.setEnabled(True)

        crc32 = CRC32Calculator.calculate(self.firmware_data)

        command = {
            "cmd": "ota_start",
            "version": version,
            "size": self.firmware_size,
            "crc32": int(crc32),
            "chunks": self.total_chunks
        }

        topic = f"device/{self.device_id}/ota/cmd"
        self.mqtt_worker.publish(topic, json.dumps(command))

        # 格式化输出发送的消息
        self.append_log(f"┌─────────────────────────────────────────────")
        self.append_log(f"│ [SEND] 主题: {topic}")
        self.append_log(f"│ [SEND] 内容: {json.dumps(command, indent=2, ensure_ascii=False)}")
        self.append_log(f"└─────────────────────────────────────────────")

        # 延迟发送第一个数据块
        QTimer.singleShot(500, self.send_next_chunk)

    def on_stop_upgrade_clicked(self):
        """停止升级按钮事件"""
        self.transfer_in_progress = False
        self.btn_start_upgrade.setEnabled(True)
        self.btn_stop_upgrade.setEnabled(False)
        self.append_log("[OTA] 升级已被用户停止")

        # 发送停止命令
        command = {"cmd": "ota_stop"}
        topic = f"device/{self.device_id}/ota/cmd"
        self.mqtt_worker.publish(topic, json.dumps(command))

        self.append_log(f"┌─────────────────────────────────────────────")
        self.append_log(f"│ [SEND] 主题: {topic}")
        self.append_log(f"│ [SEND] 内容: {json.dumps(command, indent=2, ensure_ascii=False)}")
        self.append_log(f"└─────────────────────────────────────────────")

    def send_next_chunk(self):
        """发送下一个数据块（等待ACK确认后再发送下一块）"""
        if not self.transfer_in_progress or self.sent_chunks >= self.total_chunks:
            if self.sent_chunks >= self.total_chunks and self.transfer_in_progress:
                self.append_log("[OTA] 所有数据块发送成功，等待设备校验...")
            return

        # 检查是否正在等待ACK（避免重复发送）
        if hasattr(self, 'waiting_for_ack') and self.waiting_for_ack:
            return

        start = self.sent_chunks * CHUNK_SIZE
        end = min(start + CHUNK_SIZE, self.firmware_size)
        chunk_data = self.firmware_data[start:end]

        chunk_msg = {
            "index": self.sent_chunks,
            "total": self.total_chunks,
            "size": len(chunk_data),
            "data": base64.b64encode(chunk_data).decode('ascii')
        }

        topic = f"device/{self.device_id}/ota/data"
        self.mqtt_worker.publish(topic, json.dumps(chunk_msg))

        # 格式化输出发送的数据块信息（不显示base64数据）
        chunk_info = {
            "index": self.sent_chunks,
            "total": self.total_chunks,
            "size": len(chunk_data)
        }

        self.append_log(f"┌─────────────────────────────────────────────")
        self.append_log(f"│ [SEND] 主题: {topic}")
        self.append_log(f"│ [SEND] 块信息: {json.dumps(chunk_info)}")
        self.append_log(f"│ [SEND] 状态: 等待ACK...")
        self.append_log(f"└─────────────────────────────────────────────")

        # 设置等待ACK标志
        self.waiting_for_ack = True

        # 设置ACK超时（5秒）
        self.ack_timeout_timer = QTimer()
        self.ack_timeout_timer.setSingleShot(True)
        self.ack_timeout_timer.timeout.connect(self.on_ack_timeout)
        self.ack_timeout_timer.start(5000)

    def on_ack_timeout(self):
        """ACK超时处理 - 重传当前块"""
        if self.transfer_in_progress and hasattr(self, 'waiting_for_ack') and self.waiting_for_ack:
            self.append_log(f"[OTA] 块 {self.sent_chunks} ACK超时，重新发送...")
            # 不增加sent_chunks，重新发送当前块
            self.send_next_chunk()

    def append_log(self, message):
        """添加日志"""
        if self.cb_timestamp.isChecked():
            timestamp = time.strftime("%H:%M:%S")
            log_line = f"[{timestamp}] {message}"
        else:
            log_line = message

        self.te_log.append(log_line)

        if self.cb_auto_scroll.isChecked():
            self.te_log.moveCursor(QTextCursor.End)

    def save_log(self):
        """保存日志到文件"""
        file_path, _ = QFileDialog.getSaveFileName(
            self,
            "保存日志",
            f"ota_log_{time.strftime('%Y%m%d_%H%M%S')}.txt",
            "文本文件 (*.txt);;所有文件 (*.*)"
        )

        if file_path:
            try:
                with open(file_path, 'w') as f:
                    f.write(self.te_log.toPlainText())
                self.append_log(f"[INFO] 日志已保存到: {file_path}")
            except Exception as e:
                QMessageBox.critical(self, "保存错误", f"保存日志失败: {str(e)}")

    def closeEvent(self, event):
        """关闭事件"""
        self.saveSettings()
        if self.mqtt_worker:
            self.mqtt_worker.stop()
            self.mqtt_worker.wait()
        event.accept()


def main():
    app = QApplication(sys.argv)
    app.setApplicationName("W5500 OTA Server")
    app.setOrganizationName("OTA_Server")

    # 设置深色主题
    app.setStyle("Fusion")
    palette = QPalette()
    palette.setColor(QPalette.Window, QColor(53, 53, 53))
    palette.setColor(QPalette.WindowText, Qt.white)
    palette.setColor(QPalette.Base, QColor(42, 42, 42))
    palette.setColor(QPalette.AlternateBase, QColor(66, 66, 66))
    palette.setColor(QPalette.ToolTipBase, Qt.white)
    palette.setColor(QPalette.ToolTipText, Qt.white)
    palette.setColor(QPalette.Text, Qt.white)
    palette.setColor(QPalette.Button, QColor(66, 66, 66))
    palette.setColor(QPalette.ButtonText, Qt.white)
    palette.setColor(QPalette.BrightText, Qt.red)
    palette.setColor(QPalette.Link, QColor(42, 130, 218))
    palette.setColor(QPalette.Highlight, QColor(42, 130, 218))
    palette.setColor(QPalette.HighlightedText, Qt.black)
    app.setPalette(palette)

    window = OTAServer()
    window.show()

    sys.exit(app.exec_())


if __name__ == '__main__':
    main()
