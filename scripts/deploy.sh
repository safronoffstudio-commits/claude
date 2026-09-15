#!/usr/bin/env bash
# Деплой бота на Vercel в один заход.
#   bash scripts/deploy.sh
# Токен можно передать заранее:  TELEGRAM_BOT_TOKEN=123:abc bash scripts/deploy.sh
set -euo pipefail

cd "$(dirname "$0")/.."

say() { printf '\n\033[1m%s\033[0m\n' "$*"; }
die() { printf '\n\033[31mОшибка: %s\033[0m\n' "$*" >&2; exit 1; }

command -v node >/dev/null || die "нужен Node.js 20+"

# --- 1. Токен -----------------------------------------------------------
TOKEN="${TELEGRAM_BOT_TOKEN:-}"
if [ -z "$TOKEN" ] && [ -f .env ]; then
  TOKEN="$(grep -E '^TELEGRAM_BOT_TOKEN=' .env | cut -d= -f2- || true)"
fi
if [ -z "$TOKEN" ]; then
  printf 'Токен от @BotFather: '
  read -r TOKEN
fi
[ -n "$TOKEN" ] || die "пустой токен"
case "$TOKEN" in
  *:*) ;;
  *) die "токен должен быть вида 123456789:AA..." ;;
esac

# --- 2. Секрет вебхука --------------------------------------------------
SECRET="${WEBHOOK_SECRET:-}"
if [ -z "$SECRET" ]; then
  SECRET="$(node -e 'console.log(require("crypto").randomBytes(24).toString("hex"))')"
  say "Сгенерирован WEBHOOK_SECRET (сохрани, им же открывается /api/setup):"
  echo "  $SECRET"
fi

VC="npx --yes vercel@latest"

# --- 3. Логин и привязка проекта ---------------------------------------
say "1/4 Вход в Vercel"
$VC whoami >/dev/null 2>&1 || $VC login

say "2/4 Привязка проекта"
[ -d .vercel ] || $VC link --yes

# --- 4. Переменные окружения -------------------------------------------
say "3/4 Переменные окружения"
set_env() {
  local key="$1" val="$2"
  $VC env rm "$key" production --yes >/dev/null 2>&1 || true
  printf '%s' "$val" | $VC env add "$key" production >/dev/null
  echo "  $key — записан"
}
set_env TELEGRAM_BOT_TOKEN "$TOKEN"
set_env WEBHOOK_SECRET "$SECRET"

# --- 5. Деплой ----------------------------------------------------------
say "4/4 Деплой"
DEPLOY_OUT="$($VC deploy --prod --yes)"
# CLI может печатать несколько строк — забираем последний https-адрес
URL="$(printf '%s\n' "$DEPLOY_OUT" | grep -oE 'https://[A-Za-z0-9._-]+' | tail -n 1)"
[ -n "$URL" ] || die "не удалось вычленить URL деплоя из вывода:
$DEPLOY_OUT"
echo "  $URL"

# --- 6. Вебхук ----------------------------------------------------------
say "Прописываю вебхук"
RESP="$(curl -fsS "$URL/api/setup?key=$SECRET")" || die "не удалось вызвать /api/setup.
Проверь диагностику: $URL/api/diag?key=$SECRET"
echo "$RESP"

case "$RESP" in
  *'"ok":true'*)
    say "Готово. Открывай бота и жми /start"
    echo "Лендинг: $URL"
    echo
    echo "Если бот молчит — открой диагностику:"
    echo "  $URL/api/diag?key=$SECRET"
    echo
    echo "Осталось вручную у @BotFather (по желанию):"
    echo "  /setinline — включить inline-режим, чтобы бот работал в любом чате"
    ;;
  *) die "вебхук не встал, смотри ответ выше" ;;
esac
