# リリース作成

Windowsは `scripts/build-windows.ps1 -Configuration Release`、Androidは `scripts/build-android.ps1 -Configuration Release` でビルドします。

Androidは `android/signing.properties` に以下をローカルで設定します。実際の鍵・パスワードは公開しません。

```properties
storeFile=.signing/release.jks
storePassword=YOUR_LOCAL_PASSWORD
keyAlias=snesdq5-release
keyPassword=YOUR_LOCAL_PASSWORD
```

鍵はJDKのkeytoolでRSA 4096ビット、有効期間10000日として作成します。鍵と設定はGit対象外です。同じアプリを更新するため、初回に作成した鍵を継続使用してください。鍵と設定は配布フォルダとは別の安全な場所へバックアップしてください。

`_dl/<version>/` にWindows実行ファイル、署名済みAPK、README.ME、LICENSE、licenses、対応する全ソースのZIP、SHA256SUMS.txtを配置します。ソースZIPは独自コードと固定リビジョンの依存ソースを含め、ROM・セーブ・端末設定・署名鍵・署名設定・ビルド出力を含めません。

配布前にapksignerによる署名検証、APKの16KBアライメント検査、配布ファイルとソースZIPへの秘密情報・ROM混入がないことを確認します。

開発版とリリース版は署名が異なります。開発版を使っている端末へのリリースAPKの上書きはできません。Documents/SnesDQ5等の保存データをバックアップしたうえで開発版を削除し、リリース版をインストールしてください。
