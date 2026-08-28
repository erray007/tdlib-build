# tdlib-build

TDLib'i (Telegram Database Library) GitHub'in derleme makinelerinde derleyip hazir
dosyalari indirmek icin kullanilan depo. Icinde uygulama kodu yoktur, sadece derleme
tarifi vardir.

Neden: TDLib derlenirken her kaynak dosya icin 0,5-1 GB bellek ister ve toplamda
10-15 GB gecici disk alani harcar. Bunu kisisel bilgisayarda yapmak yerine burada
yapiyoruz.

## Ne uretiliyor

**Android** (`tdlib-android` ciktisi)
- `libs/arm64-v8a/`, `libs/armeabi-v7a/`, `libs/x86_64/`, `libs/x86/` -> `libtdjni.so`
- `java/org/drinkless/tdlib/TdApi.java` ve `Client.java` -> Java/Kotlin tarafindan
  cagirmak icin gereken kod (elle yazmaya gerek yok)

**Windows** (`tdlib-windows-x64` ciktisi)
- `bin/tdjson.dll`, `lib/*.lib`, `include/td/telegram/*.h`

## Nasil calistirilir

Depoya gonderim yapildiginda otomatik baslar. Elle baslatmak icin:

```
gh workflow run "TDLib derle (Android + Windows)"
```

Belirli bir TDLib surumu icin:

```
gh workflow run "TDLib derle (Android + Windows)" -f tdlib_ref=v1.8.67
```

Varsayilan `master`, yani her calistirmada TDLib'in en guncel hali derlenir.

## Ciktilari indirme

```
gh run download <run-id> -n tdlib-android
gh run download <run-id> -n tdlib-windows-x64
```

## Lisans

Bu depodaki derleme tarifi serbesttir. Derlenen TDLib, Boost Software License 1.0
ile lisanslidir (bkz. https://github.com/tdlib/td).
