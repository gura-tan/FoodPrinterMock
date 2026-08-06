# SDカードから読み込んだUI音声クリップ(5個)を内部SRAMではなくPSRAMに
# 保持できるようにするための設定。CoreS3は8MB Octal PSRAMを搭載している。
#
# 【要確認】オプション名はESP-IDF/ターゲットのバージョンで多少揺れることがある。
# 反映後に `idf.py fullclean && idf.py build` を実行し、
# `idf.py menuconfig` の Component config -> ESP PSRAM で実際に
# 有効になっているか確認してほしい。
#
# 注意: sdkconfig本体は "Automatically generated file. DO NOT EDIT." な
# ファイルなので、このsdkconfig.defaultsをプロジェクトルートに置いてから
# fullclean + build で再生成させること(sdkconfigを直接書き換えない)。

CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y
CONFIG_SPIRAM_USE_MALLOC=y
