function pad(value, length = 2) {
  return String(value).padStart(length, "0");
}

export function formatLocalDateTime(input = new Date()) {
  const date = input instanceof Date ? input : new Date(input);
  return `${date.getFullYear()}-${pad(date.getMonth() + 1)}-${pad(date.getDate())} ${pad(
    date.getHours()
  )}:${pad(date.getMinutes())}:${pad(date.getSeconds())}`;
}

export function getLocalTimeZone() {
  return Intl.DateTimeFormat().resolvedOptions().timeZone || "Local";
}

export function nowLocal() {
  return formatLocalDateTime(new Date());
}
