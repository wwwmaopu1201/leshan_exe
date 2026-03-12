<template>
  <div id="app">
    <router-view />
  </div>
</template>

<script>
export default {
  name: 'App'
}
</script>

<style>
* {
  margin: 0;
  padding: 0;
  box-sizing: border-box;
}

html, body, #app {
  height: 100%;
  font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, "Helvetica Neue", Arial, sans-serif;
}

.app-boot {
  display: flex;
  align-items: center;
  justify-content: center;
  min-height: 100%;
  background: #f5f7fa;
  color: #303133;
}

.app-boot__card {
  min-width: 320px;
  padding: 24px 32px;
  border-radius: 12px;
  background: #ffffff;
  box-shadow: 0 12px 32px rgba(31, 35, 41, 0.08);
  text-align: center;
  font-size: 16px;
}
</style>
