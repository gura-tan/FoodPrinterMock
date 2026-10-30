# 【訂正】以前の版ではOctal Mode(CONFIG_SPIRAM_MODE_OCT)を指定していましたが、
# これはESP32-S3-BOX-3向けの設定で、CoreS3では起動クラッシュの原因になります。
# CoreS3のPSRAMチップは Quad Mode が正しいので、こちらに修正しました。
#
# menuconfig(Component config -> ESP PSRAM -> Support for external,
# SPI-connected RAM)で確認したところ、Mode/クロック/malloc設定は
# デフォルトのままで問題ありませんでした。

CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_QUAD=y
CONFIG_SPIRAM_SPEED_40M=y
CONFIG_SPIRAM_USE_MALLOC=y
