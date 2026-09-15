#!/usr/bin/env bash
# Диагностика бота с твоей машины: спрашивает Telegram напрямую.
#   bash scripts/doctor.sh                         — спросит токен и домен
#   bash scripts/doctor.sh https://твой.vercel.app — домен аргументом
set -uo pipefail
cd "$(dirname "$0")/.."

B=$'\033[1m'; R=$'\033[31m'; G=$'\033[32m'; Y=$'\033[33m'; N=$'\033[0m'
ok()   { printf '  %sOK%s      %s\n' "$G" "$N" "$*"; }
bad()  { printf '  %sПРОБЛЕМА%s %s\n' "$R" "$N" "$*"; PROBLEMS=$((PROBLEMS+1)); }
warn() { printf '  %sВНИМАНИЕ%s %s\n' "$Y" "$N" "$*"; }
head_() { printf '\n%s%s%s\n' "$B" "$*" "$N"; }
jget() { node scripts/jget.mjs "$1"; }
PROBLEMS=0

command -v node >/dev/null || { echo "нужен Node.js"; exit 1; }
command -v curl >/dev/null || { echo "нужен curl"; exit 1; }

# --- токен ---
TOKEN="${TELEGRAM_BOT_TOKEN:-}"
[ -z "$TOKEN" ] && [ -f .env ] && TOKEN="$(grep -E '^TELEGRAM_BOT_TOKEN=' .env | cut -d= -f2- || true)"
if [ -z "$TOKEN" ]; then printf 'Токен от @BotFather: '; read -r TOKEN; fi
[ -n "$TOKEN" ] || { echo "пустой токен"; exit 1; }

# --- домен ---
URL="${1:-}"
if [ -z "$URL" ]; then printf 'Домен деплоя (https://...): '; read -r URL; fi
URL="${URL%/}"
case "$URL" in https://*) ;; *) URL="https://$URL" ;; esac

head_ "1. Токен"
ME="$(curl -sS -m 20 "https://api.telegram.org/bot$TOKEN/getMe" 2>/dev/null)"
USERNAME="$(printf '%s' "$ME" | jget result.username)"
if [ -n "$USERNAME" ]; then
  ok "рабочий, бот @$USERNAME"
else
  bad "Telegram не принял токен: $(printf '%s' "$ME" | jget description)"
  echo; echo "Возьми новый токен у @BotFather (/token) и запусти снова."; exit 1
fi

head_ "2. Деплой доступен снаружи"
HDR="$(curl -sS -m 20 -o /tmp/_doc_body -w '%{http_code}' "$URL/api/telegram" 2>/dev/null)"
BODY="$(cat /tmp/_doc_body 2>/dev/null)"
if [ "$HDR" = "200" ]; then
  ok "функция отвечает"
  T_SET="$(printf '%s' "$BODY" | jget token_задан)"
  S_SET="$(printf '%s' "$BODY" | jget secret_задан)"
  [ "$T_SET" = "true" ] && ok "TELEGRAM_BOT_TOKEN задан на Vercel" \
                        || bad "TELEGRAM_BOT_TOKEN на Vercel НЕ задан — добавь и нажми Redeploy"
  [ "$S_SET" = "true" ] && ok "WEBHOOK_SECRET задан на Vercel" \
                        || bad "WEBHOOK_SECRET на Vercel НЕ задан — добавь и нажми Redeploy"
elif [ "$HDR" = "401" ] || [ "$HDR" = "403" ]; then
  bad "деплой закрыт авторизацией (HTTP $HDR)"
  echo "        Это Deployment Protection. Vercel -> Settings -> Deployment Protection -> выключить."
  echo "        Пока она включена, Telegram не достучится ни до вебхука, ни до картинки."
elif [ "$HDR" = "404" ]; then
  bad "по адресу $URL/api/telegram ничего нет — проверь домен и что деплой прошёл"
else
  bad "деплой не отвечает (HTTP $HDR)"
fi

head_ "3. Генератор картинок"
ITYPE="$(curl -sS -m 30 -o /tmp/_doc_img -w '%{content_type}' "$URL/api/og?s=doctor" 2>/dev/null)"
case "$ITYPE" in
  image/*) ok "работает ($ITYPE, $(wc -c < /tmp/_doc_img) байт)" ;;
  *)       bad "не отдаёт картинку ($ITYPE): $(head -c 200 /tmp/_doc_img 2>/dev/null)"
           echo "        Бот из-за этого не умрёт — он пришлёт мем текстом." ;;
esac

head_ "4. Вебхук глазами Telegram"
WI="$(curl -sS -m 20 "https://api.telegram.org/bot$TOKEN/getWebhookInfo" 2>/dev/null)"
W_URL="$(printf '%s' "$WI" | jget result.url)"
W_ERR="$(printf '%s' "$WI" | jget result.last_error_message)"
W_PEND="$(printf '%s' "$WI" | jget result.pending_update_count)"
WANT="$URL/api/telegram"

if [ -z "$W_URL" ]; then
  bad "вебхук НЕ ПРОПИСАН — Telegram не знает, куда слать сообщения"
elif [ "$W_URL" != "$WANT" ]; then
  bad "вебхук смотрит на $W_URL, а деплой живёт на $WANT"
else
  ok "прописан верно: $W_URL"
fi
if [ -n "$W_ERR" ]; then
  bad "последняя ошибка доставки: $W_ERR"
else
  [ -n "$W_URL" ] && ok "Telegram на доставку не жалуется"
fi
if [ -n "$W_PEND" ] && [ "$W_PEND" != "0" ]; then
  warn "в очереди $W_PEND необработанных апдейтов"
fi

head_ "ИТОГ"
if [ "$PROBLEMS" = "0" ]; then
  echo "  Всё на месте. Если бот всё равно молчит — напиши ему /start ещё раз"
  echo "  и загляни в логи: Vercel -> твой проект -> Logs."
  exit 0
fi
echo "  Найдено проблем: $PROBLEMS (см. выше)"

# --- предложить починку ---
if [ -z "$W_URL" ] || [ "$W_URL" != "$WANT" ] || [ -n "$W_ERR" ]; then
  echo
  printf 'Прописать вебхук заново? Нужен WEBHOOK_SECRET из Vercel. [y/N] '
  read -r ANS
  case "$ANS" in
    y|Y|д|Д)
      printf 'WEBHOOK_SECRET: '; read -r SEC
      echo
      # Ставим через /api/setup: сервер подставит свой секрет, и он гарантированно совпадёт
      SETUP="$(curl -sS -m 30 "$URL/api/setup?key=$SEC" 2>/dev/null)"
      if [ "$(printf '%s' "$SETUP" | jget ok)" = "true" ]; then
        ok "вебхук прописан на $(printf '%s' "$SETUP" | jget webhook)"
        echo; echo "Пиши боту /start."
      else
        bad "не вышло: $(printf '%s' "$SETUP" | jget error)"
        echo "        Если написано «неверный key» — секрет не совпадает с тем,"
        echo "        что лежит в переменных окружения Vercel."
      fi
      ;;
  esac
fi
