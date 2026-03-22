import Vue from 'vue'
import VueRouter from 'vue-router'
import Login from '@/views/Login.vue'
import Layout from '@/views/Layout.vue'

Vue.use(VueRouter)

const routes = [
  {
    path: '/login',
    name: 'Login',
    component: Login
  },
  {
    path: '/',
    component: Layout,
    redirect: '/home',
    children: [
      { path: 'home', name: 'Home', component: () => import('@/views/Home.vue') },
      { path: 'tools', name: 'Tools', component: () => import('@/views/Tools.vue') },
      { path: 'database', name: 'Database', component: () => import('@/views/Database.vue') },
      { path: 'groups', redirect: '/users' },
      { path: 'roles', name: 'Roles', component: () => import('@/views/Roles.vue') },
      { path: 'users', name: 'Users', component: () => import('@/views/Users.vue') },
      { path: 'operators', redirect: '/users' },
      { path: 'devices', name: 'Devices', component: () => import('@/views/Devices.vue') }
    ]
  }
]

const router = new VueRouter({
  mode: 'hash',
  routes
})

router.beforeEach((to, from, next) => {
  const token = localStorage.getItem('token')
  if (to.path !== '/login' && !token) {
    next('/login')
  } else if (to.path === '/login' && token) {
    next('/home')
  } else {
    next()
  }
})

export default router
