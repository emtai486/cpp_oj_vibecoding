import { createRouter, createWebHistory } from 'vue-router';
import HomePage from '../pages/HomePage.vue';
import LoginPage from '../pages/LoginPage.vue';
import RegisterPage from '../pages/RegisterPage.vue';
import ProblemListPage from '../pages/ProblemListPage.vue';
import ProblemDetailPage from '../pages/ProblemDetailPage.vue';
import SubmissionListPage from '../pages/SubmissionListPage.vue';
import MySubmissionHistoryPage from '../pages/MySubmissionHistoryPage.vue';
import AdminProblemsPage from '../pages/admin/AdminProblemsPage.vue';
import AdminTagsPage from '../pages/admin/AdminTagsPage.vue';
import AdminDifficultiesPage from '../pages/admin/AdminDifficultiesPage.vue';
import AdminTestDataPage from '../pages/admin/AdminTestDataPage.vue';
import AdminSubmissionsPage from '../pages/admin/AdminSubmissionsPage.vue';
import AdminWorkersPage from '../pages/admin/AdminWorkersPage.vue';
import AdminAiLogsPage from '../pages/admin/AdminAiLogsPage.vue';
import NotFoundPage from '../pages/NotFoundPage.vue';
import { session } from '../stores/session';

export const router = createRouter({
  history: createWebHistory(),
  routes: [
    { path: '/', name: 'home', component: HomePage },
    { path: '/login', name: 'login', component: LoginPage },
    { path: '/register', name: 'register', component: RegisterPage },
    { path: '/problems', name: 'problems', component: ProblemListPage },
    { path: '/problems/:id', name: 'problem-detail', component: ProblemDetailPage },
    { path: '/submissions', name: 'submissions', component: SubmissionListPage },
    { path: '/me/submissions', name: 'my-submissions', component: MySubmissionHistoryPage },
    { path: '/admin/problems', name: 'admin-problems', component: AdminProblemsPage },
    { path: '/admin/tags', name: 'admin-tags', component: AdminTagsPage },
    { path: '/admin/difficulties', name: 'admin-difficulties', component: AdminDifficultiesPage },
    { path: '/admin/testdata', name: 'admin-testdata', component: AdminTestDataPage },
    { path: '/admin/submissions', name: 'admin-submissions', component: AdminSubmissionsPage },
    { path: '/admin/workers', name: 'admin-workers', component: AdminWorkersPage },
    { path: '/admin/ai-logs', name: 'admin-ai-logs', component: AdminAiLogsPage },
    { path: '/:pathMatch(.*)*', name: 'not-found', component: NotFoundPage }
  ]
});

router.beforeEach((to) => {
  if (to.path.startsWith('/admin') && session.role !== 'admin') {
    return { path: '/login', query: { redirect: to.fullPath } };
  }

  if ((to.path === '/me/submissions' || to.path === '/submissions') && session.role === 'guest') {
    return { path: '/login', query: { redirect: to.fullPath } };
  }

  return true;
});
